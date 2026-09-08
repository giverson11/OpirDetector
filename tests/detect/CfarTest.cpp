#include "detect/Cfar.hpp"
#include "core/Types.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace opir {
namespace {

/// Wide enough that a ref = 6 ring fits with room to spare: the interior the
/// algorithm actually visits is rows/cols minus 2 * ref.
constexpr std::size_t kSize = 25;

struct Frame {
    std::size_t rows = kSize, cols = kSize;
    std::vector<Pixel> px = std::vector<Pixel>(kSize * kSize, 0);

    FrameSpan span() const { return FrameSpan{px.data(), rows, cols}; }
    Pixel &at(std::size_t r, std::size_t c) { return px[r * cols + c]; }
};

/// A checkerboard of two values has an exactly known ring statistic: the ring
/// is 13x13 minus the 7x7 guard = 120 cells, split 60/60 between the two, so
/// the mean is their average and sigma is half their difference. That makes
/// "k sigmas above the background" a number a test can state outright.
Frame checkerboard(Pixel low, Pixel high) {
    Frame f;
    for (std::size_t r = 0; r < f.rows; ++r)
        for (std::size_t c = 0; c < f.cols; ++c)
            f.at(r, c) = ((r + c) % 2 == 0) ? low : high;
    return f;
}

constexpr std::size_t kCentre = kSize / 2;
constexpr Pixel kLow = 1000;
constexpr Pixel kHigh = 1016;
constexpr double kRingMean = 1008.0; // (low + high) / 2
constexpr double kRingSigma = 8.0;   // (high - low) / 2

struct Output {
    std::vector<uint8_t> mask;
    std::vector<float> bg;
    std::vector<float> sg;
    bool flagged(std::size_t r, std::size_t c) const {
        return mask[r * kSize + c] != 0;
    }
    float background(std::size_t r, std::size_t c) const {
        return bg[r * kSize + c];
    }
    float sigma(std::size_t r, std::size_t c) const {
        return sg[r * kSize + c];
    }
};

/// Runs the detector over a frame, with bg pre-poisoned so a test can tell
/// "written" from "left alone".
constexpr float kPoison = -12345.0f;
Output run(const Frame &f, const CfarParams &p) {
    Output out{std::vector<uint8_t>(f.rows * f.cols, 0xFF),
               std::vector<float>(f.rows * f.cols, kPoison),
               std::vector<float>(f.rows * f.cols, kPoison)};
    cfar_threshold(f.span(), p, Plane<uint8_t>{out.mask.data(), f.rows, f.cols},
                   Plane<float>{out.bg.data(), f.rows, f.cols},
                   Plane<float>{out.sg.data(), f.rows, f.cols});
    return out;
}

// ---------------------------------------------------------------------------
// The background estimate.
// ---------------------------------------------------------------------------

/// On a flat frame every ring is flat, so the background estimate is the frame
/// value and nothing can stand out from it.
TEST(CfarBackground, EqualsTheFrameValueOnAFlatFrame) {
    Frame f;
    std::ranges::fill(f.px, static_cast<Pixel>(4000));

    const Output out = run(f, CfarParams{});

    EXPECT_FLOAT_EQ(out.background(kCentre, kCentre), 4000.0f);
    EXPECT_FALSE(out.flagged(kCentre, kCentre))
        << "a pixel equal to its own background is not a detection";
}

/// The ring is symmetric about the centre in both axes, so a linear ramp
/// averages back to exactly the centre pixel's own value.
TEST(CfarBackground, FollowsALinearRowGradient) {
    Frame f;
    for (std::size_t r = 0; r < f.rows; ++r)
        for (std::size_t c = 0; c < f.cols; ++c)
            f.at(r, c) = static_cast<Pixel>(1000 + 20 * r);

    const Output out = run(f, CfarParams{});

    for (std::size_t r = 6; r + 6 < kSize; ++r)
        EXPECT_NEAR(out.background(r, kCentre),
                    static_cast<float>(1000 + 20 * r), 1e-3f)
            << "at row " << r;
}

/// NOTE: this pins current behaviour, not desired behaviour. Border pixels are
/// skipped because their ring would fall outside the frame, and while `mask` is
/// cleared up front, `bg` is left exactly as the caller passed it in. A caller
/// reusing one buffer across frames keeps stale values on the border.
TEST(CfarBackground, IsLeftUntouchedOnTheBorder) {
    Frame f;
    std::ranges::fill(f.px, static_cast<Pixel>(4000));

    const Output out = run(f, CfarParams{});

    EXPECT_FLOAT_EQ(out.background(0, 0), kPoison);
    EXPECT_FLOAT_EQ(out.background(5, kCentre), kPoison)
        << "one row inside ref";
    EXPECT_FLOAT_EQ(out.background(kSize - 1, kSize - 1), kPoison);
    EXPECT_FLOAT_EQ(out.background(6, kCentre), 4000.0f) << "first visited row";
}

// ---------------------------------------------------------------------------
// The threshold.
// ---------------------------------------------------------------------------

TEST(CfarThreshold, FlagsAPixelJustAboveKSigmaAndNotJustBelow) {
    CfarParams p{};
    p.k = 5.0;
    const double threshold = kRingMean + p.k * kRingSigma; // 1048

    Frame below = checkerboard(kLow, kHigh);
    below.at(kCentre, kCentre) = static_cast<Pixel>(threshold - 1);
    EXPECT_FALSE(run(below, p).flagged(kCentre, kCentre));

    Frame above = checkerboard(kLow, kHigh);
    above.at(kCentre, kCentre) = static_cast<Pixel>(threshold + 1);
    EXPECT_TRUE(run(above, p).flagged(kCentre, kCentre));
}

TEST(CfarThreshold, EstimatesTheRingMeanAndSigmaFromTheCheckerboard) {
    const Output out = run(checkerboard(kLow, kHigh), CfarParams{});

    EXPECT_NEAR(out.background(kCentre, kCentre), kRingMean, 1e-3)
        << "the ring is an even split of the two values";
}

/// k is the only knob between "background" and "detection", so raising it must
/// only ever remove detections.
/// The sigma plane is what a later stage needs to turn a detection into an
/// SNR, so it has to carry the same estimate the threshold was built from.
TEST(CfarThreshold, WritesTheLocalSigmaAlongsideTheMean) {
    const Output out = run(checkerboard(kLow, kHigh), CfarParams{});

    EXPECT_NEAR(out.sigma(kCentre, kCentre), kRingSigma, 1e-3);
    EXPECT_FLOAT_EQ(out.sigma(0, 0), kPoison)
        << "like bg, sigma is only written on the interior";
}

TEST(CfarThreshold, RaisingKCanOnlyRemoveDetections) {
    Frame f = checkerboard(kLow, kHigh);
    f.at(kCentre, kCentre) = static_cast<Pixel>(kRingMean + 5 * kRingSigma + 1);

    CfarParams loose{};
    loose.k = 5.0;
    CfarParams tight{};
    tight.k = 6.0;

    EXPECT_TRUE(run(f, loose).flagged(kCentre, kCentre));
    EXPECT_FALSE(run(f, tight).flagged(kCentre, kCentre))
        << "the same pixel must fall below a stricter threshold";
}

/// The guard band is what stops a target's own skirt from being counted as the
/// background it is supposed to stand out from.
TEST(CfarGuardBand, ExcludesEnergyCloseToTheCentreFromTheBackground) {
    Frame f;
    std::ranges::fill(f.px, static_cast<Pixel>(1000));
    f.at(kCentre, kCentre + 2) = 60000; // 2 px out: inside a guard of 3

    CfarParams guarded{};
    guarded.guard = 3;
    CfarParams unguarded{};
    unguarded.guard = 0;

    EXPECT_FLOAT_EQ(run(f, guarded).background(kCentre, kCentre), 1000.0f)
        << "a neighbour inside the guard band must not enter the estimate";
    EXPECT_GT(run(f, unguarded).background(kCentre, kCentre), 1000.0f)
        << "with no guard band the same neighbour does enter it";
}

/// A frame with no room for a full ring has no interior at all, and must come
/// back empty rather than reading outside itself.
TEST(CfarThreshold, FlagsNothingWhenTheFrameIsSmallerThanTheRing) {
    CfarParams p{};
    const std::size_t n = static_cast<std::size_t>(2 * p.ref); // one short
    std::vector<Pixel> px(n * n, 1000);
    px[(n / 2) * n + n / 2] = 60000;
    std::vector<uint8_t> mask(n * n, 0xFF);
    std::vector<float> bg(n * n, kPoison), sg(n * n, kPoison);

    cfar_threshold(
        FrameSpan{px.data(), n, n}, p, Plane<uint8_t>{mask.data(), n, n},
        Plane<float>{bg.data(), n, n}, Plane<float>{sg.data(), n, n});

    EXPECT_TRUE(std::ranges::all_of(mask, [](uint8_t m) { return m == 0; }))
        << "mask is cleared even when no pixel is examined";
}

} // namespace
} // namespace opir
