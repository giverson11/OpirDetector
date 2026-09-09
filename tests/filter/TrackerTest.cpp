#include "filter/Tracker.hpp"
#include "core/Types.hpp"
#include "detect/Cluster.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace opir {
namespace {

constexpr double kDt = 0.1;

/// A detection at a position. Amplitude and snr are carried but unused by the
/// tracker, so they stay fixed.
Detection at(double row, double col, FrameId frame = 0) {
    return Detection{frame, row, col, 900.0, 9.0};
}

TEST(Tracker, ReportsATrackOnlyOnceItHasEnoughHits) {
    Tracker tracker;
    const std::vector<Detection> dets{at(10.0, 10.0)};

    EXPECT_TRUE(tracker.step(0, kDt, dets).empty());
    EXPECT_EQ(tracker.tracks().size(), 1u)
        << "the track exists, it is only tentative";
    EXPECT_TRUE(tracker.step(1, kDt, dets).empty());

    const std::vector<TrackRecord> recs = tracker.step(2, kDt, dets);
    ASSERT_EQ(recs.size(), 1u);
    EXPECT_EQ(recs[0].frame_id, 2u);
    EXPECT_DOUBLE_EQ(recs[0].row, 10.0);
    EXPECT_DOUBLE_EQ(recs[0].col, 10.0);
}

/// A confirmed track that goes unseen is still reported, on its prediction,
/// which is the whole reason for filtering. It survives max_misses frames of
/// that and no more.
TEST(Tracker, CoastsOnItsPredictionThenBuriesATrackThatKeepsMissing) {
    Tracker tracker;
    for (FrameId f = 0; f < 3; ++f) {
        const std::vector<Detection> dets{at(10.0 + f, 10.0, f)};
        tracker.step(f, kDt, dets);
    }

    const std::vector<Detection> none;
    const std::vector<TrackRecord> coasted = tracker.step(3, kDt, none);
    ASSERT_EQ(coasted.size(), 1u);
    EXPECT_NEAR(coasted[0].row, 13.0, 1e-9) << "carried on at 10 px/s";

    EXPECT_EQ(tracker.step(4, kDt, none).size(), 1u);
    EXPECT_EQ(tracker.step(5, kDt, none).size(), 1u);
    EXPECT_EQ(tracker.tracks().size(), 1u) << "three misses is still alive";

    EXPECT_TRUE(tracker.step(6, kDt, none).empty());
    EXPECT_TRUE(tracker.tracks().empty()) << "the fourth miss buries it";
}

TEST(Tracker, ClaimsADetectionInsideTheGateAndSpawnsOneOutsideIt) {
    Tracker tracker;
    const std::vector<Detection> first{at(10.0, 10.0)};
    tracker.step(0, kDt, first);

    const std::vector<Detection> near{at(12.0, 10.0)};
    tracker.step(1, kDt, near);
    ASSERT_EQ(tracker.tracks().size(), 1u)
        << "two pixels is inside the default gate, so the track absorbed it";

    // The track is now predicted at row 14, so this sits six pixels out:
    // just beyond the gate, and claimed the moment the gate is loosened.
    const std::vector<Detection> far{at(20.0, 10.0)};
    tracker.step(2, kDt, far);
    EXPECT_EQ(tracker.tracks().size(), 2u)
        << "a detection outside the gate starts its own track";
}

/// Sorting on cost alone would hand the detection to whichever track happens
/// to sit closer, and a newborn beside an established track often does.
TEST(Tracker, LetsAConfirmedTrackClaimAheadOfACloserTentativeOne) {
    Tracker tracker;
    const std::vector<Detection> steady{at(10.0, 10.0)};
    for (FrameId f = 0; f < 3; ++f)
        tracker.step(f, kDt, steady);

    const std::vector<Detection> two{at(10.0, 10.0), at(16.0, 10.0)};
    tracker.step(3, kDt, two);
    ASSERT_EQ(tracker.tracks().size(), 2u) << "the far one started a track";

    // 3.5 px from the confirmed track, 2.5 px from the tentative one.
    const std::vector<Detection> contested{at(13.5, 10.0)};
    const std::vector<TrackRecord> recs = tracker.step(4, kDt, contested);

    ASSERT_EQ(recs.size(), 1u) << "only the older track is confirmed";
    EXPECT_NEAR(recs[0].row, 12.1, 1e-12) << "10 + 0.6 * 3.5";

    ASSERT_EQ(tracker.tracks().size(), 2u);
    const Track &tentative = tracker.tracks()[1];
    EXPECT_EQ(tentative.hits, 1) << "it never got a second look";
    EXPECT_DOUBLE_EQ(tentative.filter.state().row, 16.0) << "and did not move";
}

} // namespace
} // namespace opir
