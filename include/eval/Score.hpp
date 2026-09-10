#pragma once

#include "core/Types.hpp"
#include "filter/Tracker.hpp"
#include "sim/SceneSimulator.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace opir {

/// How close a track has to fall before it counts as covering a truth record.
/// Deliberately separate from TrackParams::gate: that one decides what a track
/// is allowed to believe, this one decides what a reader is willing to call
/// correct, and tightening the second must not change the first.
struct ScoreParams {
    double match_radius = 4.0;
};

/// One truth record and the track that covered it, with the error signed as
/// track minus truth so a consistent lag reads as a consistent sign.
struct Match {
    TargetId target_id;
    TrackId track_id;
    double d_row, d_col;
    double distance;
};

/// What one frame's report amounted to against that frame's truth.
struct FrameScore {
    FrameId frame_id;
    std::vector<Match> matched;
    /// In frame, but no track within the radius is on it.
    std::vector<TargetId> missed;
    /// Reported, but no truth record within the radius explains it.
    std::vector<TrackId> false_alarms;
};

///
/// Pairs truth against tracks for one frame, nearest first.
///
/// Association is the same greedy claim Tracker::associate makes: every pair
/// inside the radius is scored, and pairs are taken best-first until nothing
/// is left. Matching each truth record to its own nearest track independently
/// would let two of them claim the same track and report no miss at all.
///
/// \param frame
/// \param truth that frame's records; a target absent here has left the frame
///        and is neither matched nor missed
/// \param tracks that frame's report
/// \param params
/// \return every truth record and every track accounted for exactly once
///
FrameScore score_frame(FrameId frame, std::span<const TruthRecord> truth,
                       std::span<const TrackRecord> tracks,
                       const ScoreParams &params = {});

///
/// Running count, mean and variance in one pass.
///
/// Welford's form rather than accumulating squares: the errors here are small
/// numbers whose squares are smaller still, and sum_sq/n - mean^2 loses its
/// significant digits exactly there.
///
class Welford {
    std::size_t n_ = 0;
    double mean_ = 0.0;
    double m2_ = 0.0;

  public:
    void add(double x);

    std::size_t count() const { return n_; }
    double mean() const { return mean_; }

    /// Sample variance, or NaN before there are two samples to spread.
    double variance() const;
    double stddev() const;
};

/// What one target's run looked like. Errors only ever come from the frames
/// where it was matched, so a miss withholds a sample rather than poisoning
/// the mean with a number no track produced.
struct TargetStats {
    TargetId target_id = 0;
    /// Frames whose truth mentions this target at all.
    std::size_t frames_present = 0;
    std::size_t frames_tracked = 0;
    std::size_t misses = 0;
    /// The frame this target was first covered, which is where the confirm
    /// delay shows up rather than hiding inside the miss count.
    std::optional<FrameId> first_tracked;

    Welford radial, d_row, d_col;
};

struct RunSummary {
    std::size_t frames = 0;
    std::size_t total_misses = 0;
    std::size_t total_false_alarms = 0;
    /// Ordered by target id.
    std::vector<TargetStats> targets;
};

///
/// Scores frame by frame and carries the totals.
///
/// Statistics are keyed by target, not by track: the target is what persists,
/// and which track happens to cover it is the outcome being measured.
///
class Scorer {
    ScoreParams params_;
    std::map<TargetId, TargetStats> targets_;
    std::size_t frames_ = 0;
    std::size_t misses_ = 0;
    std::size_t false_alarms_ = 0;

  public:
    explicit Scorer(ScoreParams params = {}) : params_{params} {}

    ///
    /// Scores one frame and folds it into the totals.
    ///
    /// \param frame
    /// \param truth
    /// \param tracks
    /// \return that frame's own result, for a caller that reports as it goes
    ///
    FrameScore add(FrameId frame, std::span<const TruthRecord> truth,
                   std::span<const TrackRecord> tracks);

    RunSummary summary() const;
};

} // namespace opir
