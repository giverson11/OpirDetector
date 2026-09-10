#include "filter/AlphaBeta.hpp"

#include <gtest/gtest.h>

namespace opir {
namespace {

TEST(AlphaBetaFilter, AppliesAlphaToPositionAndBetaToVelocity) {
    AlphaBetaFilter f{10.0, 20.0};

    EXPECT_DOUBLE_EQ(f.state().row, 10.0);
    EXPECT_DOUBLE_EQ(f.state().v_row, 0.0);

    f.predict(0.5);
    EXPECT_DOUBLE_EQ(f.state().row, 10.0) << "no velocity means no motion";
    EXPECT_DOUBLE_EQ(f.error_magnitude(13.0, 24.0), 5.0) << "hypot(3, 4)";

    f.update(0.5, 15.0, 24.0);
    EXPECT_DOUBLE_EQ(f.state().row, 13.0) << "10 + 0.6 * 5";
    EXPECT_DOUBLE_EQ(f.state().col, 22.4) << "20 + 0.6 * 4";
    EXPECT_DOUBLE_EQ(f.state().v_row, 2.0) << "0.2 * 5 / 0.5";
    EXPECT_DOUBLE_EQ(f.state().v_col, 1.6) << "0.2 * 4 / 0.5";

    f.predict(0.5);
    EXPECT_DOUBLE_EQ(f.state().row, 14.0) << "13 + 2 * 0.5";
    EXPECT_DOUBLE_EQ(f.state().col, 23.2);

    const StateEstimate before = f.state();
    f.update(0.0, 30.0, 23.2);
    EXPECT_GT(f.state().row, before.row) << "the position still moves";
    EXPECT_DOUBLE_EQ(f.state().v_row, before.v_row)
        << "a non-positive dt leaves the velocity alone";
}

TEST(AlphaBetaFilter, ConvergesOnAConstantVelocityTrack) {
    constexpr double kDt = 0.1, kVRow = 30.0, kVCol = -12.0;
    const auto row = [](int frame) { return 5.0 + kVRow * kDt * frame; };
    const auto col = [](int frame) { return 40.0 + kVCol * kDt * frame; };

    AlphaBetaFilter f{row(0), col(0)};
    for (int frame = 1; frame <= 40; ++frame) {
        f.predict(kDt);
        f.update(kDt, row(frame), col(frame));
    }

    EXPECT_NEAR(f.state().row, row(40), 1e-6);
    EXPECT_NEAR(f.state().col, col(40), 1e-6);
    EXPECT_NEAR(f.state().v_row, kVRow, 1e-5);
    EXPECT_NEAR(f.state().v_col, kVCol, 1e-5);
}

} // namespace
} // namespace opir
