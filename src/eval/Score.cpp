#include "eval/Score.hpp"
#include "core/Types.hpp"
#include "filter/Tracker.hpp"
#include "sim/SceneSimulator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace opir {
namespace {

/// The member order is the sort order: cheapest first, with the indices
/// breaking exact ties so the same input always scores the same way.
struct Pair {
    double cost;
    std::size_t truth, track;
    friend auto operator<=>(const Pair &, const Pair &) = default;
};

} // namespace

FrameScore score_frame(FrameId frame, std::span<const TruthRecord> truth,
                       std::span<const TrackRecord> tracks,
                       const ScoreParams &params) {
    FrameScore score{
        .frame_id = frame, .matched = {}, .missed = {}, .false_alarms = {}};

    std::vector<Pair> pairs;
    for (std::size_t ti = 0; ti < truth.size(); ++ti) {
        for (std::size_t ki = 0; ki < tracks.size(); ++ki) {
            const double cost = std::hypot(tracks[ki].row - truth[ti].row,
                                           tracks[ki].col - truth[ti].col);
            if (cost <= params.match_radius) {
                pairs.push_back({cost, ti, ki});
            }
        }
    }
    std::ranges::sort(pairs);

    std::vector<char> truth_taken(truth.size(), 0);
    std::vector<char> track_taken(tracks.size(), 0);
    for (const Pair &p : pairs) {
        if (truth_taken[p.truth] || track_taken[p.track]) {
            continue;
        }
        truth_taken[p.truth] = 1;
        track_taken[p.track] = 1;
        score.matched.push_back({.target_id = truth[p.truth].target_id,
                                 .track_id = tracks[p.track].track_id,
                                 .d_row = tracks[p.track].row - truth[p.truth].row,
                                 .d_col = tracks[p.track].col - truth[p.truth].col,
                                 .distance = p.cost});
    }

    for (std::size_t ti = 0; ti < truth.size(); ++ti) {
        if (!truth_taken[ti]) {
            score.missed.push_back(truth[ti].target_id);
        }
    }
    for (std::size_t ki = 0; ki < tracks.size(); ++ki) {
        if (!track_taken[ki]) {
            score.false_alarms.push_back(tracks[ki].track_id);
        }
    }
    return score;
}

void Welford::add(double x) {
    ++n_;
    const double delta = x - mean_;
    mean_ += delta / static_cast<double>(n_);
    // The second delta uses the updated mean; that pairing is what keeps the
    // sum of squares from drifting.
    m2_ += delta * (x - mean_);
}

double Welford::variance() const {
    if (n_ < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return m2_ / static_cast<double>(n_ - 1);
}

double Welford::stddev() const { return std::sqrt(variance()); }

FrameScore Scorer::add(FrameId frame, std::span<const TruthRecord> truth,
                       std::span<const TrackRecord> tracks) {
    const FrameScore score = score_frame(frame, truth, tracks, params_);

    ++frames_;
    misses_ += score.missed.size();
    false_alarms_ += score.false_alarms.size();

    // Presence comes from truth, not from the match: a target counts as
    // present the moment it is in frame, whether or not anything found it.
    for (const TruthRecord &record : truth) {
        TargetStats &stats = targets_[record.target_id];
        stats.target_id = record.target_id;
        ++stats.frames_present;
    }

    for (const Match &match : score.matched) {
        TargetStats &stats = targets_[match.target_id];
        ++stats.frames_tracked;
        if (!stats.first_tracked) {
            stats.first_tracked = frame;
        }
        stats.radial.add(match.distance);
        stats.d_row.add(match.d_row);
        stats.d_col.add(match.d_col);
    }

    for (const TargetId missed : score.missed) {
        ++targets_[missed].misses;
    }

    return score;
}

RunSummary Scorer::summary() const {
    RunSummary out{.frames = frames_,
                   .total_misses = misses_,
                   .total_false_alarms = false_alarms_,
                   .targets = {}};
    out.targets.reserve(targets_.size());
    for (const auto &[id, stats] : targets_) {
        out.targets.push_back(stats);
    }
    return out;
}

} // namespace opir
