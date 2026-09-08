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
constexpr std::size_t kRef = 6;
static_assert(kRef == static_cast<std::size_t>(CfarParams{}.ref),
              "the tests below assume the default ring");

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
    std::vector<double> bg;
    std::vector<double> sg;
    bool flagged(std::size_t r, std::size_t c) const {
        return mask[r * kSize + c] != 0;
    }
    bool any_flagged() const {
        return std::ranges::any_of(mask, [](uint8_t m) { return m != 0; });
    }
    double background(std::size_t r, std::size_t c) const {
        return bg[r * kSize + c];
    }
    double sigma(std::size_t r, std::size_t c) const {
        return sg[r * kSize + c];
    }
};

/// Runs the detector over a frame, with bg and sg pre-poisoned so a test can
/// tell "written" from "left alone".
constexpr double kPoison = -12345.0;
Output run(const Frame &f, const CfarParams &p) {
    Output out{std::vector<uint8_t>(f.rows * f.cols, 0xFF),
               std::vector<double>(f.rows * f.cols, kPoison),
               std::vector<double>(f.rows * f.cols, kPoison)};
    cfar_threshold(f.span(), p, Plane<uint8_t>{out.mask.data(), f.rows, f.cols},
                   Plane<double>{out.bg.data(), f.rows, f.cols},
                   Plane<double>{out.sg.data(), f.rows, f.cols});
    return out;
}

// ---------------------------------------------------------------------------
// The background estimate.
// ---------------------------------------------------------------------------

/// The ring is symmetric about the centre in both axes, so a linear ramp
/// averages back to exactly the centre pixel's own value, and a pixel equal to
/// its own background is never a detection. A flat frame is the gradient-zero
/// case of the same thing.
TEST(CfarBackground, FollowsALinearRowGradientAndFlagsNothing) {
    Frame f;
    for (std::size_t r = 0; r < f.rows; ++r)
        for (std::size_t c = 0; c < f.cols; ++c)
            f.at(r, c) = static_cast<Pixel>(1000 + 20 * r);

    const Output out = run(f, CfarParams{});

    for (std::size_t r = kRef; r + kRef < kSize; ++r)
        EXPECT_NEAR(out.background(r, kCentre),
                    static_cast<double>(1000 + 20 * r), 1e-3)
            << "at row " << r;
    EXPECT_FALSE(out.any_flagged())
        << "no pixel stands out from a ramp its ring predicts exactly";
}

/// The sigma plane is what a later stage needs to turn a detection into an
/// SNR, so it has to carry the same estimate the threshold was built from.
///
/// NOTE: the border assertions pin current behaviour, not desired behaviour.
/// Border pixels are skipped because their ring would fall outside the frame,
/// and while `mask` is cleared up front, `bg` and `sg` are left exactly as the
/// caller passed them in. A caller reusing one buffer across frames keeps
/// stale values on the border.
TEST(CfarBackground, WritesTheRingMeanAndSigmaOnTheInteriorOnly) {
    const Output out = run(checkerboard(kLow, kHigh), CfarParams{});

    EXPECT_NEAR(out.background(kCentre, kCentre), kRingMean, 1e-3)
        << "the ring is an even split of the two values";
    EXPECT_NEAR(out.sigma(kCentre, kCentre), kRingSigma, 1e-3);

    EXPECT_DOUBLE_EQ(out.background(0, 0), kPoison);
    EXPECT_DOUBLE_EQ(out.sigma(0, 0), kPoison);
    EXPECT_DOUBLE_EQ(out.background(kRef - 1, kCentre), kPoison)
        << "one row inside ref";
    EXPECT_NE(out.background(kRef, kCentre), kPoison) << "first visited row";
    EXPECT_NE(out.background(kSize - kRef - 1, kCentre), kPoison)
        << "last visited row";
    EXPECT_DOUBLE_EQ(out.background(kSize - kRef, kCentre), kPoison)
        << "one row inside ref at the far edge: visiting it would read past "
           "the frame";
}

// ---------------------------------------------------------------------------
// The threshold.
// ---------------------------------------------------------------------------

/// k is the only knob between "background" and "detection": one count either
/// side of mean + k * sigma is the whole decision.
TEST(CfarThreshold, FlagsAPixelJustAboveKSigmaAndNotJustBelow) {
    CfarParams p{};
    p.k = 5.0;
    const double threshold = kRingMean + p.k * kRingSigma; // 1048

    Frame below = checkerboard(kLow, kHigh);
    below.at(kCentre, kCentre) = static_cast<Pixel>(threshold - 1);
    EXPECT_FALSE(run(below, p).flagged(kCentre, kCentre));

    Frame exact = checkerboard(kLow, kHigh);
    exact.at(kCentre, kCentre) = static_cast<Pixel>(threshold);
    EXPECT_FALSE(run(exact, p).flagged(kCentre, kCentre))
        << "the comparison is strict: at the threshold is not above it";

    Frame above = checkerboard(kLow, kHigh);
    above.at(kCentre, kCentre) = static_cast<Pixel>(threshold + 1);
    EXPECT_TRUE(run(above, p).flagged(kCentre, kCentre));
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

    EXPECT_DOUBLE_EQ(run(f, guarded).background(kCentre, kCentre), 1000.0)
        << "a neighbour inside the guard band must not enter the estimate";
    EXPECT_GT(run(f, unguarded).background(kCentre, kCentre), 1000.0)
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
    std::vector<double> bg(n * n, kPoison), sg(n * n, kPoison);

    cfar_threshold(
        FrameSpan{px.data(), n, n}, p, Plane<uint8_t>{mask.data(), n, n},
        Plane<double>{bg.data(), n, n}, Plane<double>{sg.data(), n, n});

    EXPECT_TRUE(std::ranges::all_of(mask, [](uint8_t m) { return m == 0; }))
        << "mask is cleared even when no pixel is examined";
}

} // namespace
} // namespace opir
