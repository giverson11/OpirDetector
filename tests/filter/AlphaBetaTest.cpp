#include "filter/AlphaBeta.hpp"
#include "core/Error.hpp"

#include <gtest/gtest.h>

namespace opir {
namespace {

TEST(AlphaBetaFilter, RejectsGainsOutsideTheStableRegion) {
    EXPECT_NO_THROW((AlphaBetaFilter{0.0, 0.0, AlphaBetaParams{}}));
    EXPECT_NO_THROW((AlphaBetaFilter{0.0, 0.0, AlphaBetaParams{1.9, 0.15}}));

    EXPECT_THROW((AlphaBetaFilter{0.0, 0.0, AlphaBetaParams{0.6, 0.0}}), Error);
    EXPECT_THROW((AlphaBetaFilter{0.0, 0.0, AlphaBetaParams{0.0, 0.2}}), Error);
    EXPECT_THROW((AlphaBetaFilter{0.0, 0.0, AlphaBetaParams{0.6, 2.8}}), Error)
        << "beta must stay below 4 - 2 * alpha";
}

/// A new track has a position and no idea of its speed, so the second
/// detection is taken whole and its velocity read straight off the difference.
TEST(AlphaBetaFilter, TakesItsVelocityFromTheSecondDetection) {
    AlphaBetaFilter f{10.0, 20.0};

    EXPECT_DOUBLE_EQ(f.state().row, 10.0);
    EXPECT_DOUBLE_EQ(f.state().v_row, 0.0);

    f.predict(0.5);
    EXPECT_DOUBLE_EQ(f.state().row, 10.0) << "no velocity means no motion";
    EXPECT_DOUBLE_EQ(f.distance(13.0, 24.0), 5.0) << "hypot(3, 4)";

    f.update(0.5, 15.0, 24.0);
    EXPECT_DOUBLE_EQ(f.state().row, 15.0);
    EXPECT_DOUBLE_EQ(f.state().col, 24.0);
    EXPECT_DOUBLE_EQ(f.state().v_row, 10.0) << "(15 - 10) / 0.5";
    EXPECT_DOUBLE_EQ(f.state().v_col, 8.0) << "(24 - 20) / 0.5";

    f.predict(0.5);
    EXPECT_DOUBLE_EQ(f.state().row, 20.0) << "15 + 10 * 0.5";
    EXPECT_DOUBLE_EQ(f.state().col, 28.0);
}

/// With the velocity taken from the second detection there is nothing left to
/// converge to, so a clean constant-velocity track is followed exactly. Fixed
/// gains from the start would still be a few thousandths out after twenty
/// frames.
TEST(AlphaBetaFilter, FollowsANoiselessConstantVelocityTrackExactly) {
    constexpr double kDt = 0.1, kVRow = 30.0, kVCol = -12.0;
    const auto row = [](int frame) { return 5.0 + kVRow * kDt * frame; };
    const auto col = [](int frame) { return 40.0 + kVCol * kDt * frame; };

    AlphaBetaFilter f{row(0), col(0)};
    for (int frame = 1; frame <= 20; ++frame) {
        f.predict(kDt);
        f.update(kDt, row(frame), col(frame));
    }

    EXPECT_NEAR(f.state().row, row(20), 1e-9);
    EXPECT_NEAR(f.state().col, col(20), 1e-9);
    EXPECT_NEAR(f.state().v_row, kVRow, 1e-9);
    EXPECT_NEAR(f.state().v_col, kVCol, 1e-9);
}

/// The opening gains decay into the pair the caller asked for, and stay there.
TEST(AlphaBetaFilter, SettlesToTheSteadyStateGains) {
    AlphaBetaFilter f{0.0, 0.0};
    // Four updates on the spot: the residual is zero, so nothing moves and
    // only the gain schedule advances.
    for (int i = 0; i < 4; ++i)
        f.update(2.0, 0.0, 0.0);

    f.update(2.0, 10.0, 0.0);
    EXPECT_DOUBLE_EQ(f.state().row, 6.0) << "an alpha of 0.6 absorbs 60% of 10";
    EXPECT_DOUBLE_EQ(f.state().v_row, 1.0) << "a beta of 0.2: 0.2 * 10 / 2";

    const StateEstimate before = f.state();
    f.update(0.0, 20.0, 0.0);
    EXPECT_GT(f.state().row, before.row) << "the position still moves";
    EXPECT_DOUBLE_EQ(f.state().v_row, before.v_row)
        << "a non-positive dt leaves the velocity alone";
}

} // namespace
} // namespace opir
