#include "sim/SceneSimulator.hpp"
#include "core/Error.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <numeric>
#include <utility>
#include <vector>

namespace opir {
namespace {

/// std::normal_distribution is only defined for a positive standard deviation,
/// so "noise off" means a sigma small enough that every sample it draws is
/// erased again by the rounding in the simulator's quantizer.
constexpr double kNoNoise = 1e-12;
constexpr uint64_t kSeed = 42;

/// A background of exactly zero with both noise sources silenced: whatever a
/// pixel holds after this is the contribution of the targets alone.
SceneParams quiet_params() {
    return SceneParams{.mean = 0.0,
                       .fpn_sigma = kNoNoise,
                       .read_sigma = kNoNoise,
                       .dc_level = 0.0,
                       .row_gradient = 0.0};
}

/// A rendered buffer plus the shape needed to address it by (row, column).
struct Image {
    std::size_t rows{};
    std::size_t columns{};
    std::vector<uint16_t> pixels;

    uint16_t at(std::size_t r, std::size_t c) const {
        return pixels[r * columns + c];
    }
    double sum() const {
        return std::accumulate(pixels.begin(), pixels.end(), 0.0);
    }
};

Image render(SceneSimulator &simulator, std::size_t rows, std::size_t columns,
             double t = 0.0) {
    Image image{.rows = rows, .columns = columns,
                .pixels = std::vector<uint16_t>(rows * columns)};
    simulator.render(t, image.pixels);
    return image;
}

double mean_of(const std::vector<uint16_t> &values) {
    return std::accumulate(values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
}

/// Sample standard deviation (Bessel-corrected).
double stddev_of(const std::vector<uint16_t> &values) {
    const double mean = mean_of(values);
    double sum_squares = 0.0;
    for (const uint16_t value : values) {
        const double deviation = static_cast<double>(value) - mean;
        sum_squares += deviation * deviation;
    }
    return std::sqrt(sum_squares / static_cast<double>(values.size() - 1));
}

// ---------------------------------------------------------------------------
// SceneParams, one field at a time.
// ---------------------------------------------------------------------------

/// dc_level is the flat pedestal every pixel sits on.
TEST(SceneParamsDcLevel, SetsEveryPixelWhenNothingElseContributes) {
    SceneParams params = quiet_params();
    params.dc_level = 1234.0;
    SceneSimulator simulator(8, 8, params, kSeed);

    const Image image = render(simulator, 8, 8);

    for (const uint16_t pixel : image.pixels)
        EXPECT_EQ(pixel, 1234);
}

/// The buffer is uint16_t, so the pedestal is rounded and clamped into range.
TEST(SceneParamsDcLevel, IsRoundedToTheNearestCount) {
    SceneParams params = quiet_params();
    params.dc_level = 100.6;
    SceneSimulator simulator(4, 4, params, kSeed);

    EXPECT_EQ(render(simulator, 4, 4).at(0, 0), 101);
}

TEST(SceneParamsDcLevel, ClampsBelowZeroAndAboveSaturation) {
    SceneParams dark = quiet_params();
    dark.dc_level = -500.0;
    SceneSimulator dark_simulator(4, 4, dark, kSeed);
    EXPECT_EQ(render(dark_simulator, 4, 4).at(0, 0), 0);

    SceneParams bright = quiet_params();
    bright.dc_level = 70000.0;
    SceneSimulator bright_simulator(4, 4, bright, kSeed);
    EXPECT_EQ(render(bright_simulator, 4, 4).at(0, 0), 65535);
}

/// row_gradient adds a fixed step per row and nothing across a row.
TEST(SceneParamsRowGradient, AddsALinearRampDownRowsOnly) {
    SceneParams params = quiet_params();
    params.dc_level = 1000.0;
    params.row_gradient = 25.0;
    SceneSimulator simulator(16, 12, params, kSeed);

    const Image image = render(simulator, 16, 12);

    for (std::size_t r = 0; r < image.rows; ++r) {
        const uint16_t expected =
            static_cast<uint16_t>(1000 + 25 * static_cast<int>(r));
        for (std::size_t c = 0; c < image.columns; ++c)
            EXPECT_EQ(image.at(r, c), expected) << "at row " << r << ", column " << c;
    }
}

TEST(SceneParamsRowGradient, MayBeNegativeAndClampsAtZero) {
    SceneParams params = quiet_params();
    params.dc_level = 100.0;
    params.row_gradient = -10.0;
    SceneSimulator simulator(16, 4, params, kSeed);

    const Image image = render(simulator, 16, 4);

    EXPECT_EQ(image.at(0, 0), 100);
    EXPECT_EQ(image.at(5, 0), 50);
    EXPECT_EQ(image.at(10, 0), 0);
    EXPECT_EQ(image.at(15, 0), 0) << "a ramp below zero must clamp, not wrap";
}

/// NOTE: `mean` is handed to *both* the fixed-pattern distribution and the read
/// noise distribution, so it lands on every pixel twice. This test pins the
/// behavior as it stands; see the review note if a single offset was intended.
TEST(SceneParamsMean, OffsetsEveryPixelOncePerNoiseSource) {
    SceneParams params = quiet_params();
    params.dc_level = 1000.0;
    params.mean = 50.0;
    SceneSimulator simulator(8, 8, params, kSeed);

    const Image image = render(simulator, 8, 8);

    for (const uint16_t pixel : image.pixels)
        EXPECT_EQ(pixel, 1100);
}

/// Fixed-pattern noise is per-pixel and, as the name says, fixed: it is drawn
/// once at construction and repeats in every frame.
TEST(SceneParamsFpnSigma, IsIdenticalInEveryFrame) {
    SceneParams params = quiet_params();
    params.dc_level = 10000.0;
    params.fpn_sigma = 50.0;
    SceneSimulator simulator(32, 32, params, kSeed);

    const Image first = render(simulator, 32, 32, 0.0);
    const Image second = render(simulator, 32, 32, 1.0);

    EXPECT_EQ(first.pixels, second.pixels);
}

TEST(SceneParamsFpnSigma, SetsTheSpatialSpreadAboutDcLevel) {
    SceneParams params = quiet_params();
    params.dc_level = 10000.0;
    params.fpn_sigma = 50.0;
    SceneSimulator simulator(128, 128, params, kSeed);

    const Image image = render(simulator, 128, 128);

    EXPECT_NEAR(mean_of(image.pixels), 10000.0, 5.0);
    EXPECT_NEAR(stddev_of(image.pixels), 50.0, 5.0);
}

TEST(SceneParamsFpnSigma, LeavesAFlatFrameWhenZero) {
    SceneParams params = quiet_params();
    params.dc_level = 10000.0;
    SceneSimulator simulator(32, 32, params, kSeed);

    const Image image = render(simulator, 32, 32);

    EXPECT_DOUBLE_EQ(stddev_of(image.pixels), 0.0);
}

/// Read noise is redrawn per pixel per frame, so it varies in time where the
/// fixed pattern does not.
TEST(SceneParamsReadSigma, VariesFromFrameToFrame) {
    SceneParams params = quiet_params();
    params.dc_level = 10000.0;
    params.read_sigma = 30.0;
    SceneSimulator simulator(32, 32, params, kSeed);

    const Image first = render(simulator, 32, 32, 0.0);
    const Image second = render(simulator, 32, 32, 0.0);

    EXPECT_NE(first.pixels, second.pixels)
        << "read noise must be redrawn, even at the same timestamp";
}

TEST(SceneParamsReadSigma, SetsTheTemporalSpreadOfASinglePixel) {
    constexpr std::size_t kFrames = 512;
    SceneParams params = quiet_params();
    params.dc_level = 10000.0;
    params.read_sigma = 30.0;
    SceneSimulator simulator(8, 8, params, kSeed);

    std::vector<uint16_t> history;
    history.reserve(kFrames);
    for (std::size_t frame = 0; frame < kFrames; ++frame)
        history.push_back(render(simulator, 8, 8, static_cast<double>(frame)).at(3, 5));

    EXPECT_NEAR(mean_of(history), 10000.0, 5.0);
    EXPECT_NEAR(stddev_of(history), 30.0, 3.0);
}

/// The seed is what makes a run reproducible; it drives both noise sources.
TEST(SceneSimulatorSeed, ReproducesAFrameExactlyAndDiffersAcrossSeeds) {
    SceneParams params = quiet_params();
    params.dc_level = 10000.0;
    params.fpn_sigma = 50.0;
    params.read_sigma = 30.0;

    SceneSimulator a(32, 32, params, kSeed);
    SceneSimulator b(32, 32, params, kSeed);
    SceneSimulator c(32, 32, params, kSeed + 1);

    EXPECT_EQ(render(a, 32, 32).pixels, render(b, 32, 32).pixels);
    EXPECT_NE(render(a, 32, 32).pixels, render(c, 32, 32).pixels);
}

TEST(SceneSimulatorRender, RejectsABufferSmallerThanTheFrame) {
    SceneSimulator simulator(8, 8, quiet_params(), kSeed);
    std::vector<uint16_t> too_small(8 * 8 - 1);
    EXPECT_THROW(simulator.render(0.0, too_small), Error);

    std::vector<uint16_t> exact(8 * 8);
    EXPECT_NO_THROW(simulator.render(0.0, exact));
}

// ---------------------------------------------------------------------------
// Target rendering: shape, placement and energy of the Gaussian blob.
// ---------------------------------------------------------------------------

constexpr std::size_t kGrid = 96;
constexpr double kCenter = 48.0;
constexpr std::size_t kCenterIndex = 48;

/// A centred, motionless target on an otherwise empty frame.
Image render_centered_target(double amplitude, double sigma) {
    SceneSimulator simulator(kGrid, kGrid, quiet_params(), kSeed);
    simulator.add_target(Target{.r0 = kCenter,
                                .c0 = kCenter,
                                .r_rate = 0.0,
                                .c_rate = 0.0,
                                .amplitude = amplitude,
                                .sigma = sigma});
    return render(simulator, kGrid, kGrid);
}

TEST(TargetRender, IsSymmetricAboutItsCenter) {
    const Image image = render_centered_target(5000.0, 4.0);
    constexpr std::size_t center = kCenterIndex;

    for (std::size_t d = 1; d <= 20; ++d) {
        EXPECT_EQ(image.at(center + d, center), image.at(center - d, center))
            << "row symmetry broken at offset " << d;
        EXPECT_EQ(image.at(center, center + d), image.at(center, center - d))
            << "column symmetry broken at offset " << d;
        EXPECT_EQ(image.at(center + d, center + d), image.at(center - d, center - d))
            << "diagonal symmetry broken at offset " << d;
        // The blob is circular, so swapping the two offsets must not matter.
        EXPECT_EQ(image.at(center + d, center + 2 * d),
                  image.at(center + 2 * d, center + d))
            << "isotropy broken at offset " << d;
    }
}

TEST(TargetRender, PeaksAtTheTargetCenterWithTheTargetAmplitude) {
    const Image image = render_centered_target(5000.0, 4.0);
    constexpr std::size_t center = kCenterIndex;

    const auto peak = std::max_element(image.pixels.begin(), image.pixels.end());
    const std::size_t index =
        static_cast<std::size_t>(std::distance(image.pixels.begin(), peak));

    EXPECT_EQ(index / kGrid, center);
    EXPECT_EQ(index % kGrid, center);
    EXPECT_EQ(image.at(center, center), 5000);
}

TEST(TargetRender, PeakFollowsTheTargetRatesOverTime) {
    SceneSimulator simulator(kGrid, kGrid, quiet_params(), kSeed);
    simulator.add_target(Target{.r0 = 10.0,
                                .c0 = 50.0,
                                .r_rate = 5.0,
                                .c_rate = -4.0,
                                .amplitude = 5000.0,
                                .sigma = 4.0});

    const Image image = render(simulator, kGrid, kGrid, 6.0);

    const auto peak = std::max_element(image.pixels.begin(), image.pixels.end());
    const std::size_t index =
        static_cast<std::size_t>(std::distance(image.pixels.begin(), peak));

    EXPECT_EQ(index / kGrid, 40u) << "row should be r0 + r_rate * t";
    EXPECT_EQ(index % kGrid, 26u) << "column should be c0 + c_rate * t";
}

TEST(TargetRender, FallsOffMonotonicallyAwayFromThePeak) {
    const Image image = render_centered_target(5000.0, 4.0);
    constexpr std::size_t center = kCenterIndex;

    // Walk out along four rays and require each step to be no brighter than the
    // last, and strictly darker while still well above the quantization floor.
    const auto walk = [&](int row_step, int column_step, const char *name) {
        for (std::size_t d = 0; d + 1 < kCenterIndex; ++d) {
            const auto sample = [&](std::size_t k) {
                return image.at(center + k * static_cast<std::size_t>(row_step),
                                center + k * static_cast<std::size_t>(column_step));
            };
            const uint16_t here = sample(d);
            const uint16_t next = sample(d + 1);
            EXPECT_GE(here, next) << name << " ray rose again at offset " << d;
            if (here > 1) {
                EXPECT_GT(here, next)
                    << name << " ray flattened at offset " << d;
            }
        }
    };

    walk(1, 0, "down");
    walk(0, 1, "right");
    walk(1, 1, "diagonal");
}

/// A 2-D Gaussian integrates to amplitude * 2 * pi * sigma^2, and summing the
/// pixels of a well-contained blob is that integral sampled on a unit lattice.
class TargetEnergyTest
    : public testing::TestWithParam<std::pair<double, double>> {};

TEST_P(TargetEnergyTest, TotalsTheAnalyticGaussianIntegral) {
    const auto [amplitude, sigma] = GetParam();
    const Image image = render_centered_target(amplitude, sigma);

    const double expected =
        amplitude * 2.0 * std::numbers::pi * sigma * sigma;

    EXPECT_NEAR(image.sum(), expected, 0.01 * expected);
}

INSTANTIATE_TEST_SUITE_P(Blobs, TargetEnergyTest,
                         testing::Values(std::pair{1000.0, 1.5},
                                         std::pair{1000.0, 3.0},
                                         std::pair{5000.0, 4.0},
                                         std::pair{250.0, 6.0}));

TEST(TargetRender, EnergyOfTwoTargetsAdds) {
    SceneSimulator simulator(kGrid, kGrid, quiet_params(), kSeed);
    simulator.add_target(Target{.r0 = 24.0, .c0 = 24.0, .r_rate = 0.0,
                                .c_rate = 0.0, .amplitude = 2000.0, .sigma = 3.0});
    simulator.add_target(Target{.r0 = 70.0, .c0 = 70.0, .r_rate = 0.0,
                                .c_rate = 0.0, .amplitude = 800.0, .sigma = 2.0});

    const Image image = render(simulator, kGrid, kGrid);

    const double expected = 2.0 * std::numbers::pi *
                            (2000.0 * 9.0 + 800.0 * 4.0);

    EXPECT_NEAR(image.sum(), expected, 0.01 * expected);
}

} // namespace
} // namespace opir
