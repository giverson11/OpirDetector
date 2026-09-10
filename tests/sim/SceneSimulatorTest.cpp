#include "sim/SceneSimulator.hpp"
#include "core/Error.hpp"
#include "core/Types.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <numeric>
#include <string>
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
constexpr double kCenter = 48.0;

/// One row past the last row (and column) of the frame.
constexpr double kPastEdge = static_cast<double>(kGrid);

/// A rendered buffer plus the shape needed to address it by (row, column).
struct Image {
    std::size_t rows{};
    std::size_t columns{};
    std::vector<Pixel> pixels;

    Pixel at(std::size_t r, std::size_t c) const {
        return pixels[r * columns + c];
    }
};

Image render(SceneSimulator &simulator, std::size_t rows, std::size_t columns,
             FrameId frame = 0) {
    Image image{.rows = rows,
                .columns = columns,
                .pixels = std::vector<Pixel>(rows * columns)};
    simulator.render(frame, Plane<Pixel>{image.pixels.data(), rows, columns});
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

double mean_of(const std::vector<Pixel> &values) {
    return std::accumulate(values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
}

/// Sample standard deviation (Bessel-corrected).
double stddev_of(const std::vector<Pixel> &values) {
    const double mean = mean_of(values);
    double sum_squares = 0.0;
    for (const Pixel value : values) {
        const double deviation = static_cast<double>(value) - mean;
        sum_squares += deviation * deviation;
    }
    return std::sqrt(sum_squares / static_cast<double>(values.size() - 1));
}

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

std::vector<TargetId> ids_of(const std::vector<TruthRecord> &records) {
    std::vector<TargetId> ids;
    for (const TruthRecord &record : records)
        ids.push_back(record.target_id);
    return ids;
}

// ---------------------------------------------------------------------------
// SceneParams, one field at a time.
// ---------------------------------------------------------------------------

/// dc_level is the flat pedestal every pixel sits on. The buffer is 16-bit, so
/// it is rounded to the nearest count and clamped into range.
TEST(SceneParamsDcLevel, IsRoundedAndClampedIntoPixelRange) {
    SceneParams params = quiet_params();
    params.dc_level = 100.6;
    SceneSimulator simulator(8, 8, params, kSeed);
    for (const Pixel pixel : render(simulator, 8, 8).pixels)
        EXPECT_EQ(pixel, 101);

    SceneParams dark = quiet_params();
    dark.dc_level = -500.0;
    SceneSimulator dark_simulator(4, 4, dark, kSeed);
    EXPECT_EQ(render(dark_simulator, 4, 4).at(0, 0), 0);

    SceneParams bright = quiet_params();
    bright.dc_level = 70000.0;
    SceneSimulator bright_simulator(4, 4, bright, kSeed);
    EXPECT_EQ(render(bright_simulator, 4, 4).at(0, 0), 65535);
}

/// row_gradient adds a fixed step per row and nothing across a row; a ramp
/// that goes below zero clamps rather than wraps.
TEST(SceneParamsRowGradient, AddsALinearRampDownRowsOnlyAndClampsAtZero) {
    SceneParams params = quiet_params();
    params.dc_level = 100.0;
    params.row_gradient = -10.0;
    SceneSimulator simulator(16, 12, params, kSeed);

    const Image image = render(simulator, 16, 12);

    for (std::size_t r = 0; r < image.rows; ++r) {
        const Pixel expected =
            static_cast<Pixel>(std::max(0, 100 - 10 * static_cast<int>(r)));
        for (std::size_t c = 0; c < image.columns; ++c)
            EXPECT_EQ(image.at(r, c), expected)
                << "at row " << r << ", column " << c;
    }
}

/// NOTE: `mean` is handed to *both* the fixed-pattern distribution and the read
/// noise distribution, so it lands on every pixel twice. This test pins the
/// behavior as it stands; see the review note if a single offset was intended.
TEST(SceneParamsMean, OffsetsEveryPixelOncePerNoiseSource) {
    SceneParams params = quiet_params();
    params.dc_level = 1000.0;
    params.mean = 50.0;
    SceneSimulator simulator(8, 8, params, kSeed);

    for (const Pixel pixel : render(simulator, 8, 8).pixels)
        EXPECT_EQ(pixel, 1100);
}

/// Fixed-pattern noise is per-pixel and, as the name says, fixed: drawn once
/// at construction with the requested spread, then repeated in every frame.
TEST(SceneParamsFpnSigma, IsFixedAcrossFramesWithTheRequestedSpread) {
    SceneParams params = quiet_params();
    params.dc_level = 10000.0;
    params.fpn_sigma = 50.0;
    SceneSimulator simulator(128, 128, params, kSeed);

    const Image first = render(simulator, 128, 128, 0);
    const Image second = render(simulator, 128, 128, 1);

    EXPECT_EQ(first.pixels, second.pixels);
    EXPECT_NEAR(mean_of(first.pixels), 10000.0, 5.0);
    EXPECT_NEAR(stddev_of(first.pixels), 50.0, 5.0);
}

/// Read noise is redrawn per pixel per frame, so one pixel's history across
/// frames has the requested spread where the fixed pattern would have none.
TEST(SceneParamsReadSigma, SetsTheTemporalSpreadOfASinglePixel) {
    constexpr std::size_t kFrames = 512;
    SceneParams params = quiet_params();
    params.dc_level = 10000.0;
    params.read_sigma = 30.0;
    SceneSimulator simulator(8, 8, params, kSeed);

    std::vector<Pixel> history;
    history.reserve(kFrames);
    for (std::size_t frame = 0; frame < kFrames; ++frame)
        history.push_back(
            render(simulator, 8, 8, static_cast<FrameId>(frame)).at(3, 5));

    EXPECT_NEAR(mean_of(history), 10000.0, 5.0);
    EXPECT_NEAR(stddev_of(history), 30.0, 3.0);
}

/// dt converts a frame index into seconds. It only shows up through target
/// motion, so this renders one moving target and looks at where its peak lands.
std::pair<std::size_t, std::size_t> moving_peak(double dt, FrameId frame) {
    SceneSimulator simulator = simulator_with(dt, {Target{.r0 = 10.0,
                                                          .c0 = 50.0,
                                                          .r_rate = 5.0,
                                                          .c_rate = -4.0,
                                                          .amplitude = 5000.0,
                                                          .sigma = 4.0}});
    return peak_of(render(simulator, kGrid, kGrid, frame));
}

TEST(SceneParamsDt, ScalesTheFrameIndexIntoSecondsOfTargetMotion) {
    using Peak = std::pair<std::size_t, std::size_t>;
    EXPECT_EQ(moving_peak(0.5, 0), (Peak{10, 50})) << "frame 0 is always t = 0";
    EXPECT_EQ(moving_peak(0.5, 12), (Peak{40, 26}))
        << "r0 + r_rate * 6 s, c0 + c_rate * 6 s";
    EXPECT_EQ(moving_peak(0.25, 12), (Peak{25, 38}))
        << "the same frame index at half the dt is half the distance";
    EXPECT_EQ(moving_peak(0.0, 1000), (Peak{10, 50})) << "no dt, no motion";
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
    // The shape now travels with the buffer, so "too small" is a view that
    // spans fewer pixels than the frame, not a short allocation behind a
    // full-size view -- that would be a read past the end, not an error.
    std::vector<Pixel> too_small(8 * 8);
    EXPECT_THROW(simulator.render(0, Plane<Pixel>{too_small.data(), 8, 7}),
                 Error);

    std::vector<Pixel> exact(8 * 8);
    EXPECT_NO_THROW(simulator.render(0, Plane<Pixel>{exact.data(), 8, 8}));
}

// ---------------------------------------------------------------------------
// Target rendering.
// ---------------------------------------------------------------------------

/// Every pixel of a two-target frame against the closed form
/// sum_i A_i * exp(-d_i^2 / 2 sigma_i^2), rounded. One comparison implies the
/// peak position and amplitude, symmetry, isotropy, monotone fall-off, the
/// Gaussian integral and additivity, so none of those needs its own test.
TEST(TargetRender, MatchesTheAnalyticGaussianPixelForPixel) {
    const Target a{.r0 = 24.0,
                   .c0 = 24.0,
                   .r_rate = 0.0,
                   .c_rate = 0.0,
                   .amplitude = 2000.0,
                   .sigma = 3.0};
    const Target b{.r0 = 70.0,
                   .c0 = 70.0,
                   .r_rate = 0.0,
                   .c_rate = 0.0,
                   .amplitude = 800.0,
                   .sigma = 2.0};
    SceneSimulator simulator = simulator_with(1.0, {a, b});

    const Image image = render(simulator, kGrid, kGrid);

    EXPECT_EQ(image.at(24, 24), 2000) << "the peak is the amplitude";
    EXPECT_EQ(image.at(70, 70), 800);

    std::size_t mismatches = 0;
    std::string first;
    for (std::size_t r = 0; r < kGrid; ++r)
        for (std::size_t c = 0; c < kGrid; ++c) {
            double expected = 0.0;
            for (const Target &t : {a, b}) {
                const double dr = static_cast<double>(r) - t.r0;
                const double dc = static_cast<double>(c) - t.c0;
                expected += t.amplitude * std::exp(-(dr * dr + dc * dc) /
                                                   (2.0 * t.sigma * t.sigma));
            }
            // Half a count is the rounding; the epsilon absorbs a value that
            // sits exactly on a rounding boundary and could go either way.
            if (std::abs(image.at(r, c) - expected) > 0.5 + 1e-6) {
                if (mismatches++ == 0)
                    first = "first at (" + std::to_string(r) + ", " +
                            std::to_string(c) + "): got " +
                            std::to_string(image.at(r, c)) + ", expected " +
                            std::to_string(expected);
            }
        }
    EXPECT_EQ(mismatches, 0u) << first;
}

// ---------------------------------------------------------------------------
// getTargetRecords: the per-frame truth table that pairs with the imagery.
// ---------------------------------------------------------------------------

/// NOTE: pins the asymmetry as it stands: a simulator holding no targets at
/// all throws, while one whose targets have all walked off the frame returns
/// an empty table (see the test below).
TEST(TargetRecords, ThrowsWhenNoTargetHasBeenAdded) {
    SceneSimulator simulator(kGrid, kGrid, quiet_params(), kSeed);

    EXPECT_THROW(simulator.getTargetRecords(0), Error);
}

/// One record per target, id by insertion order, stamped with the requested
/// frame, positioned by r0 + r_rate * frame * dt, carrying the amplitude.
TEST(TargetRecords, ReportsEveryFieldForEveryTargetAtTheRequestedFrame) {
    // Frame 8 at dt = 0.25 is t = 2 seconds.
    SceneSimulator simulator = simulator_with(0.25, {Target{.r0 = 20.0,
                                                            .c0 = 30.0,
                                                            .r_rate = 4.0,
                                                            .c_rate = 8.0,
                                                            .amplitude = 5000.0,
                                                            .sigma = 4.0},
                                                     Target{.r0 = 50.0,
                                                            .c0 = 10.0,
                                                            .r_rate = -2.0,
                                                            .c_rate = 3.0,
                                                            .amplitude = 1234.0,
                                                            .sigma = 4.0}});

    const std::vector<TruthRecord> records = simulator.getTargetRecords(8);

    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(ids_of(records), (std::vector<TargetId>{0, 1}));
    for (const TruthRecord &record : records)
        EXPECT_EQ(record.frame_id, 8u);
    EXPECT_DOUBLE_EQ(records[0].row, 28.0) << "20 + 4 * 2";
    EXPECT_DOUBLE_EQ(records[0].col, 46.0) << "30 + 8 * 2";
    EXPECT_DOUBLE_EQ(records[0].amplitude, 5000.0);
    EXPECT_DOUBLE_EQ(records[1].row, 46.0) << "50 - 2 * 2";
    EXPECT_DOUBLE_EQ(records[1].col, 16.0) << "10 + 3 * 2";
    EXPECT_DOUBLE_EQ(records[1].amplitude, 1234.0);
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

/// A target that has left the frame drops out of the table without renumbering
/// the ones that remain; once every target has left, the table is empty.
TEST(TargetRecords, KeepsIdsStableAndDropsTargetsThatHaveLeftTheFrame) {
    SceneSimulator simulator =
        simulator_with(1.0, {static_target(10.0, 10.0), departing_target(),
                             static_target(50.0, 50.0)});

    EXPECT_EQ(ids_of(simulator.getTargetRecords(0)),
              (std::vector<TargetId>{0, 1, 2}));
    EXPECT_EQ(ids_of(simulator.getTargetRecords(1)),
              (std::vector<TargetId>{0, 2}))
        << "an id must stay the insertion index, not the row of the table";

    SceneSimulator alone = simulator_with(1.0, {departing_target()});
    EXPECT_TRUE(alone.getTargetRecords(1).empty())
        << "row 100 is past the last row of a " << kGrid << "-row frame";
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
              (std::vector<TargetId>{0, 1}))
        << "the frame is inclusive of 0 and exclusive of rows_ / columns_";
}

} // namespace
} // namespace opir
