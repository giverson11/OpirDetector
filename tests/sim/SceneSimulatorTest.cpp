#include "sim/SceneSimulator.hpp"
#include "core/Error.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
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
/// pixel holds after this is the contribution of the targets alone. dt of 1
/// makes a frame index and a timestamp the same number, so tests that do not
/// care about the frame-to-seconds mapping can ignore it.
SceneParams quiet_params() {
    return SceneParams{.mean = 0.0,
                       .fpn_sigma = kNoNoise,
                       .read_sigma = kNoNoise,
                       .dc_level = 0.0,
                       .row_gradient = 0.0,
                       .dt = 1.0};
}

/// A frame big enough to hold a blob well clear of every edge.
constexpr std::size_t kGrid = 96;
constexpr std::size_t kCenterIndex = 48;
constexpr double kCenter = 48.0;

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
             std::uint32_t frame = 0) {
    Image image{.rows = rows,
                .columns = columns,
                .pixels = std::vector<uint16_t>(rows * columns)};
    simulator.render(frame, image.pixels);
    return image;
}

/// The (row, column) of the brightest pixel in an image.
std::pair<std::size_t, std::size_t> peak_of(const Image &image) {
    const auto peak =
        std::max_element(image.pixels.begin(), image.pixels.end());
    const std::size_t index =
        static_cast<std::size_t>(std::distance(image.pixels.begin(), peak));
    return {index / image.columns, index % image.columns};
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
            EXPECT_EQ(image.at(r, c), expected)
                << "at row " << r << ", column " << c;
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

    const Image first = render(simulator, 32, 32, 0);
    const Image second = render(simulator, 32, 32, 1);

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

    const Image first = render(simulator, 32, 32, 0);
    const Image second = render(simulator, 32, 32, 0);

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
        history.push_back(
            render(simulator, 8, 8, static_cast<std::uint32_t>(frame))
                .at(3, 5));

    EXPECT_NEAR(mean_of(history), 10000.0, 5.0);
    EXPECT_NEAR(stddev_of(history), 30.0, 3.0);
}

/// dt converts a frame index into seconds. It only shows up through target
/// motion, so these render one moving target and look at where its peak lands.
Image moving_target_frame(double dt, std::uint32_t frame) {
    SceneParams params = quiet_params();
    params.dt = dt;
    SceneSimulator simulator(kGrid, kGrid, params, kSeed);
    simulator.add_target(Target{.r0 = 10.0,
                                .c0 = kCenter,
                                .r_rate = 4.0,
                                .c_rate = 0.0,
                                .amplitude = 5000.0,
                                .sigma = 4.0});
    return render(simulator, kGrid, kGrid, frame);
}

std::size_t moving_peak_row(double dt, std::uint32_t frame) {
    return peak_of(moving_target_frame(dt, frame)).first;
}

TEST(SceneParamsDt, ScalesTheFrameIndexIntoSeconds) {
    EXPECT_EQ(moving_peak_row(0.5, 0), 10u) << "frame 0 is always t = 0";
    EXPECT_EQ(moving_peak_row(0.5, 8), 26u) << "10 + 4 * (8 * 0.5)";
    EXPECT_EQ(moving_peak_row(0.25, 8), 18u) << "10 + 4 * (8 * 0.25)";
}

TEST(SceneParamsDt, GivesTheSameSceneForTheSameElapsedTime) {
    // Frame 3 at dt = 1.0 and frame 6 at dt = 0.5 are both t = 3 seconds.
    EXPECT_EQ(moving_target_frame(1.0, 3).pixels,
              moving_target_frame(0.5, 6).pixels);
}

TEST(SceneParamsDt, FreezesTheSceneWhenZero) {
    EXPECT_EQ(moving_peak_row(0.0, 0), 10u);
    EXPECT_EQ(moving_peak_row(0.0, 1000), 10u) << "no dt means no motion";
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
    EXPECT_THROW(simulator.render(0, too_small), Error);

    std::vector<uint16_t> exact(8 * 8);
    EXPECT_NO_THROW(simulator.render(0, exact));
}

// ---------------------------------------------------------------------------
// Target rendering: shape, placement and energy of the Gaussian blob.
// ---------------------------------------------------------------------------

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
        EXPECT_EQ(image.at(center + d, center + d),
                  image.at(center - d, center - d))
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

    const auto [peak_row, peak_column] = peak_of(image);

    EXPECT_EQ(peak_row, center);
    EXPECT_EQ(peak_column, center);
    EXPECT_EQ(image.at(center, center), 5000);
}

TEST(TargetRender, PeakFollowsTheTargetRatesOverTime) {
    // Frame 12 at dt = 0.5 is t = 6 seconds.
    SceneParams params = quiet_params();
    params.dt = 0.5;
    SceneSimulator simulator(kGrid, kGrid, params, kSeed);
    simulator.add_target(Target{.r0 = 10.0,
                                .c0 = 50.0,
                                .r_rate = 5.0,
                                .c_rate = -4.0,
                                .amplitude = 5000.0,
                                .sigma = 4.0});

    const auto [peak_row, peak_column] =
        peak_of(render(simulator, kGrid, kGrid, 12));

    EXPECT_EQ(peak_row, 40u) << "row should be r0 + r_rate * frame * dt";
    EXPECT_EQ(peak_column, 26u) << "column should be c0 + c_rate * frame * dt";
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
                                center +
                                    k * static_cast<std::size_t>(column_step));
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

    const double expected = amplitude * 2.0 * std::numbers::pi * sigma * sigma;

    EXPECT_NEAR(image.sum(), expected, 0.01 * expected);
}

INSTANTIATE_TEST_SUITE_P(Blobs, TargetEnergyTest,
                         testing::Values(std::pair{1000.0, 1.5},
                                         std::pair{1000.0, 3.0},
                                         std::pair{5000.0, 4.0},
                                         std::pair{250.0, 6.0}));

TEST(TargetRender, EnergyOfTwoTargetsAdds) {
    SceneSimulator simulator(kGrid, kGrid, quiet_params(), kSeed);
    simulator.add_target(Target{.r0 = 24.0,
                                .c0 = 24.0,
                                .r_rate = 0.0,
                                .c_rate = 0.0,
                                .amplitude = 2000.0,
                                .sigma = 3.0});
    simulator.add_target(Target{.r0 = 70.0,
                                .c0 = 70.0,
                                .r_rate = 0.0,
                                .c_rate = 0.0,
                                .amplitude = 800.0,
                                .sigma = 2.0});

    const Image image = render(simulator, kGrid, kGrid);

    const double expected =
        2.0 * std::numbers::pi * (2000.0 * 9.0 + 800.0 * 4.0);

    EXPECT_NEAR(image.sum(), expected, 0.01 * expected);
}

// ---------------------------------------------------------------------------
// getTargetRecords: the per-frame truth table that pairs with the imagery.
// ---------------------------------------------------------------------------

/// One row past the last row (and column) of the frame.
constexpr double kPastEdge = static_cast<double>(kGrid);

Target static_target(double row, double column, double amplitude = 5000.0) {
    return Target{.r0 = row,
                  .c0 = column,
                  .r_rate = 0.0,
                  .c_rate = 0.0,
                  .amplitude = amplitude,
                  .sigma = 4.0};
}

/// A target starting near the bottom edge and walking off it after one frame.
Target departing_target() {
    return Target{.r0 = 90.0,
                  .c0 = kCenter,
                  .r_rate = 10.0,
                  .c_rate = 0.0,
                  .amplitude = 5000.0,
                  .sigma = 4.0};
}

SceneSimulator simulator_with(double dt,
                              std::initializer_list<Target> targets) {
    SceneParams params = quiet_params();
    params.dt = dt;
    SceneSimulator simulator(kGrid, kGrid, params, kSeed);
    for (const Target &target : targets)
        simulator.add_target(target);
    return simulator;
}

std::vector<std::uint32_t> ids_of(const std::vector<TruthRecord> &records) {
    std::vector<std::uint32_t> ids;
    for (const TruthRecord &record : records)
        ids.push_back(record.target_id);
    return ids;
}

TEST(TargetRecords, ThrowsWhenNoTargetHasBeenAdded) {
    SceneSimulator simulator(kGrid, kGrid, quiet_params(), kSeed);

    EXPECT_THROW(simulator.getTargetRecords(0), Error);
}

TEST(TargetRecords, ReturnsOneRecordPerTargetIdentifiedByInsertionOrder) {
    SceneSimulator simulator = simulator_with(1.0, {static_target(10.0, 20.0),
                                                    static_target(30.0, 40.0),
                                                    static_target(50.0, 60.0)});

    const std::vector<TruthRecord> records = simulator.getTargetRecords(0);

    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(ids_of(records), (std::vector<std::uint32_t>{0, 1, 2}));
    EXPECT_DOUBLE_EQ(records[0].row, 10.0);
    EXPECT_DOUBLE_EQ(records[0].col, 20.0);
    EXPECT_DOUBLE_EQ(records[2].row, 50.0);
    EXPECT_DOUBLE_EQ(records[2].col, 60.0);
}

TEST(TargetRecords, StampsEveryRecordWithTheRequestedFrameId) {
    SceneSimulator simulator = simulator_with(
        1.0, {static_target(10.0, 20.0), static_target(30.0, 40.0)});

    for (const std::uint32_t frame : {0u, 1u, 7u, 1000u}) {
        const std::vector<TruthRecord> records =
            simulator.getTargetRecords(frame);

        ASSERT_EQ(records.size(), 2u) << "at frame " << frame;
        for (const TruthRecord &record : records)
            EXPECT_EQ(record.frame_id, frame);
    }
}

TEST(TargetRecords, CarriesTheTargetAmplitude) {
    SceneSimulator simulator =
        simulator_with(1.0, {static_target(10.0, 20.0, 1234.0),
                             static_target(30.0, 40.0, 250.0)});

    const std::vector<TruthRecord> records = simulator.getTargetRecords(0);

    ASSERT_EQ(records.size(), 2u);
    EXPECT_DOUBLE_EQ(records[0].amplitude, 1234.0);
    EXPECT_DOUBLE_EQ(records[1].amplitude, 250.0);
}

TEST(TargetRecords, AdvancesThePositionWithFrameIdAndDt) {
    // Frame 8 at dt = 0.25 is t = 2 seconds.
    SceneSimulator simulator = simulator_with(0.25, {Target{.r0 = 20.0,
                                                            .c0 = 30.0,
                                                            .r_rate = 4.0,
                                                            .c_rate = 8.0,
                                                            .amplitude = 5000.0,
                                                            .sigma = 4.0}});

    const std::vector<TruthRecord> records = simulator.getTargetRecords(8);

    ASSERT_EQ(records.size(), 1u);
    EXPECT_DOUBLE_EQ(records[0].row, 28.0) << "r0 + r_rate * frame * dt";
    EXPECT_DOUBLE_EQ(records[0].col, 46.0) << "c0 + c_rate * frame * dt";
}

/// The truth table is only worth anything if it names the pixel the target was
/// actually drawn on, so this checks the two against each other on one frame.
TEST(TargetRecords, AgreeWithWhereTheTargetIsRendered) {
    SceneSimulator simulator = simulator_with(0.25, {Target{.r0 = 20.0,
                                                            .c0 = 30.0,
                                                            .r_rate = 4.0,
                                                            .c_rate = 8.0,
                                                            .amplitude = 5000.0,
                                                            .sigma = 4.0}});

    const std::vector<TruthRecord> records = simulator.getTargetRecords(8);
    const auto [peak_row, peak_column] =
        peak_of(render(simulator, kGrid, kGrid, 8));

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(peak_row, static_cast<std::size_t>(records[0].row));
    EXPECT_EQ(peak_column, static_cast<std::size_t>(records[0].col));
}

TEST(TargetRecords, ReturnsAnEmptyTableOnceEveryTargetHasLeftTheFrame) {
    // Note the asymmetry being pinned down here: a simulator holding no targets
    // at all throws, while one whose targets have all walked off the frame
    // returns an empty table.
    SceneSimulator simulator = simulator_with(1.0, {departing_target()});

    EXPECT_EQ(simulator.getTargetRecords(0).size(), 1u);
    EXPECT_TRUE(simulator.getTargetRecords(1).empty())
        << "row 100 is past the last row of a " << kGrid << "-row frame";
}

TEST(TargetRecords, KeepsTargetIdsStableWhenAnEarlierTargetLeaves) {
    SceneSimulator simulator =
        simulator_with(1.0, {static_target(10.0, 10.0), departing_target(),
                             static_target(50.0, 50.0)});

    EXPECT_EQ(ids_of(simulator.getTargetRecords(0)),
              (std::vector<std::uint32_t>{0, 1, 2}));
    EXPECT_EQ(ids_of(simulator.getTargetRecords(1)),
              (std::vector<std::uint32_t>{0, 2}))
        << "an id must stay the insertion index, not the row of the table";
}

TEST(TargetRecords, IncludesTheFirstPixelAndExcludesTheOneJustPastTheLast) {
    SceneSimulator simulator =
        simulator_with(1.0, {
                                static_target(0.0, 0.0),
                                static_target(kPastEdge - 0.5, kPastEdge - 0.5),
                                static_target(kPastEdge, 0.0),
                                static_target(0.0, kPastEdge),
                                static_target(-0.5, 0.0),
                            });

    EXPECT_EQ(ids_of(simulator.getTargetRecords(0)),
              (std::vector<std::uint32_t>{0, 1}))
        << "the frame is inclusive of 0 and exclusive of rows_ / columns_";
}

} // namespace
} // namespace opir
