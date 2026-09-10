#include "eval/Score.hpp"
#include "core/Types.hpp"
#include "filter/Tracker.hpp"
#include "sim/SceneSimulator.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace opir {
namespace {

TruthRecord truth(TargetId target, double row, double col, FrameId frame = 0) {
    return TruthRecord{.frame_id = frame,
                       .target_id = target,
                       .row = row,
                       .col = col,
                       .amplitude = 2000.0};
}

TrackRecord track(TrackId id, double row, double col, FrameId frame = 0) {
    return TrackRecord{.frame_id = frame,
                       .track_id = id,
                       .row = row,
                       .col = col,
                       .v_row = 0.0,
                       .v_col = 0.0};
}

// ---------------------------------------------------------------------------
// Association.

/// The error is signed track minus truth, so a lag keeps its direction rather
/// than being folded into a magnitude before anyone can see it.
TEST(ScoreFrame, PairsATrackWithItsTruthAndSignsTheError) {
    const std::vector<TruthRecord> t{truth(0, 10.0, 20.0)};
    const std::vector<TrackRecord> k{track(7, 9.5, 21.0)};

    const FrameScore score = score_frame(0, t, k);

    ASSERT_EQ(score.matched.size(), 1u);
    EXPECT_TRUE(score.missed.empty());
    EXPECT_TRUE(score.false_alarms.empty());

    EXPECT_EQ(score.matched[0].target_id, 0u);
    EXPECT_EQ(score.matched[0].track_id, 7u);
    EXPECT_DOUBLE_EQ(score.matched[0].d_row, -0.5);
    EXPECT_DOUBLE_EQ(score.matched[0].d_col, 1.0);
    EXPECT_DOUBLE_EQ(score.matched[0].distance, std::hypot(0.5, 1.0));
}

/// The reason association is greedy over all pairs rather than nearest-per-
/// truth: one track sits closest to both targets. Matching each truth record
/// independently would hand it to both and report no miss at all.
TEST(ScoreFrame, RefusesToLetTwoTargetsClaimTheSameTrack) {
    const std::vector<TruthRecord> t{truth(0, 10.0, 10.0),
                                     truth(1, 11.0, 10.0)};
    const std::vector<TrackRecord> k{track(0, 10.2, 10.0)};

    const FrameScore score = score_frame(0, t, k);

    ASSERT_EQ(score.matched.size(), 1u);
    EXPECT_EQ(score.matched[0].target_id, 0u) << "the closer target wins";
    ASSERT_EQ(score.missed.size(), 1u);
    EXPECT_EQ(score.missed[0], 1u);
    EXPECT_TRUE(score.false_alarms.empty());
}

/// Equal counts are not a clean frame. A track that wandered onto noise while
/// a real target went unseen is one miss and one false alarm, and counting
/// tracks against truth records would call it a match.
TEST(ScoreFrame, CountsAMissAndAFalseAlarmWhenTheCountsStillAgree) {
    const std::vector<TruthRecord> t{truth(0, 10.0, 10.0),
                                     truth(1, 50.0, 50.0)};
    const std::vector<TrackRecord> k{track(0, 10.1, 10.1),
                                     track(1, 80.0, 80.0)};

    const FrameScore score = score_frame(0, t, k);

    EXPECT_EQ(t.size(), k.size()) << "the trap this test exists for";
    ASSERT_EQ(score.matched.size(), 1u);
    ASSERT_EQ(score.missed.size(), 1u);
    ASSERT_EQ(score.false_alarms.size(), 1u);
    EXPECT_EQ(score.missed[0], 1u);
    EXPECT_EQ(score.false_alarms[0], 1u);
}

/// The radius is a hard edge, and it is the scorer's own, not the tracker's.
TEST(ScoreFrame, TreatsATrackOutsideTheRadiusAsNoMatchAtAll) {
    const std::vector<TruthRecord> t{truth(0, 0.0, 0.0)};
    const std::vector<TrackRecord> k{track(0, 0.0, 5.0)};

    EXPECT_EQ(
        score_frame(0, t, k, ScoreParams{.match_radius = 4.0}).matched.size(),
        0u);
    EXPECT_EQ(
        score_frame(0, t, k, ScoreParams{.match_radius = 6.0}).matched.size(),
        1u);
}

/// A frame every target has left is not a run of misses: with no truth there
/// is nothing to miss, and any track reported is unexplained.
TEST(ScoreFrame, ScoresEmptyTruthAndEmptyTracksWithoutInventingEither) {
    const std::vector<TrackRecord> k{track(3, 1.0, 1.0)};
    const FrameScore no_truth = score_frame(0, {}, k);
    EXPECT_TRUE(no_truth.matched.empty());
    EXPECT_TRUE(no_truth.missed.empty());
    ASSERT_EQ(no_truth.false_alarms.size(), 1u);
    EXPECT_EQ(no_truth.false_alarms[0], 3u);

    const std::vector<TruthRecord> t{truth(2, 1.0, 1.0)};
    const FrameScore no_tracks = score_frame(0, t, {});
    EXPECT_TRUE(no_tracks.matched.empty());
    ASSERT_EQ(no_tracks.missed.size(), 1u);
    EXPECT_EQ(no_tracks.missed[0], 2u);
    EXPECT_TRUE(no_tracks.false_alarms.empty());
}

// ---------------------------------------------------------------------------
// Accumulation.

/// Welford against a hand-computed mean and sample standard deviation.
TEST(ScorerSummary, AccumulatesMeanAndStandardDeviationPerTarget) {
    Scorer scorer;
    // Column error of +1, +2, +3 over three frames: mean 2, sample sd 1.
    for (FrameId f = 0; f < 3; ++f) {
        const std::vector<TruthRecord> t{truth(0, 10.0, 10.0, f)};
        const std::vector<TrackRecord> k{
            track(0, 10.0, 10.0 + 1.0 + static_cast<double>(f), f)};
        scorer.add(f, t, k);
    }

    const RunSummary summary = scorer.summary();
    ASSERT_EQ(summary.targets.size(), 1u);
    const TargetStats &stats = summary.targets[0];

    EXPECT_EQ(stats.frames_present, 3u);
    EXPECT_EQ(stats.frames_tracked, 3u);
    EXPECT_EQ(stats.misses, 0u);
    EXPECT_DOUBLE_EQ(stats.d_col.mean(), 2.0);
    EXPECT_DOUBLE_EQ(stats.d_col.stddev(), 1.0);
    EXPECT_DOUBLE_EQ(stats.d_row.mean(), 0.0);
    EXPECT_DOUBLE_EQ(stats.radial.mean(), 2.0);
}

/// A miss withholds a sample rather than contributing a zero or a radius: the
/// mean has to describe the frames a track actually produced, or a tracker
/// that misses half the run looks better than one that never does.
TEST(ScorerSummary, CountsAMissWithoutFeedingTheErrorStatistics) {
    Scorer scorer;
    const std::vector<TruthRecord> present{truth(0, 10.0, 10.0)};

    scorer.add(0, present, std::vector<TrackRecord>{track(0, 10.0, 12.0)});
    scorer.add(1, present, {});
    scorer.add(2, present, std::vector<TrackRecord>{track(0, 10.0, 12.0)});

    const RunSummary summary = scorer.summary();
    EXPECT_EQ(summary.frames, 3u);
    EXPECT_EQ(summary.total_misses, 1u);
    EXPECT_EQ(summary.total_false_alarms, 0u);

    ASSERT_EQ(summary.targets.size(), 1u);
    const TargetStats &stats = summary.targets[0];
    EXPECT_EQ(stats.frames_present, 3u);
    EXPECT_EQ(stats.frames_tracked, 2u);
    EXPECT_EQ(stats.misses, 1u);
    EXPECT_EQ(stats.radial.count(), 2u) << "the missed frame is not a sample";
    EXPECT_DOUBLE_EQ(stats.d_col.mean(), 2.0);
}

/// A target out of frame is absent from truth, so it is neither present nor
/// missed. Otherwise every target that flies off the edge would accumulate
/// misses for the rest of the run.
TEST(ScorerSummary, DoesNotChargeAMissToATargetThatHasLeftTheFrame) {
    Scorer scorer;
    scorer.add(0, std::vector<TruthRecord>{truth(0, 10.0, 10.0, 0)},
               std::vector<TrackRecord>{track(0, 10.0, 10.0, 0)});
    scorer.add(1, {}, {});
    scorer.add(2, {}, {});

    const RunSummary summary = scorer.summary();
    EXPECT_EQ(summary.total_misses, 0u);
    ASSERT_EQ(summary.targets.size(), 1u);
    EXPECT_EQ(summary.targets[0].frames_present, 1u);
    EXPECT_EQ(summary.targets[0].misses, 0u);
}

/// The confirm delay is structural, not a fault, so it is reported as the
/// frame a target was acquired rather than buried in the miss count.
TEST(ScorerSummary, RecordsTheFrameATargetWasFirstTracked) {
    Scorer scorer;
    const std::vector<TruthRecord> present{truth(0, 10.0, 10.0)};
    scorer.add(0, present, {});
    scorer.add(1, present, {});
    scorer.add(2, present, std::vector<TrackRecord>{track(0, 10.0, 10.0)});
    scorer.add(3, present, std::vector<TrackRecord>{track(0, 10.0, 10.0)});

    const RunSummary summary = scorer.summary();
    ASSERT_EQ(summary.targets.size(), 1u);
    ASSERT_TRUE(summary.targets[0].first_tracked.has_value());
    EXPECT_EQ(*summary.targets[0].first_tracked, 2u);
    EXPECT_EQ(summary.targets[0].misses, 2u);
}

TEST(ScorerSummary, LeavesATargetThatWasNeverTrackedWithoutAnAcquisition) {
    Scorer scorer;
    scorer.add(0, std::vector<TruthRecord>{truth(4, 10.0, 10.0)}, {});

    const RunSummary summary = scorer.summary();
    ASSERT_EQ(summary.targets.size(), 1u);
    EXPECT_EQ(summary.targets[0].target_id, 4u);
    EXPECT_FALSE(summary.targets[0].first_tracked.has_value());
    EXPECT_EQ(summary.targets[0].misses, 1u);
}

/// One sample has no spread to report, and a NaN says so rather than a zero
/// that would read as a perfectly repeatable measurement.
TEST(Welford, ReportsNoSpreadUntilThereAreTwoSamples) {
    ScoreForm w;
    EXPECT_EQ(w.count(), 0u);
    EXPECT_TRUE(std::isnan(w.stddev()));

    w.add(5.0);
    EXPECT_EQ(w.count(), 1u);
    EXPECT_DOUBLE_EQ(w.mean(), 5.0);
    EXPECT_TRUE(std::isnan(w.stddev()));

    w.add(7.0);
    EXPECT_DOUBLE_EQ(w.mean(), 6.0);
    EXPECT_DOUBLE_EQ(w.variance(), 2.0);
}

} // namespace
} // namespace opir
