#include "filter/Tracker.hpp"

#include <algorithm>
#include <cstddef>

namespace opir {

// Counting the miss up front is what makes "misses == 0" mean "claimed this
// frame", which associate() tests instead of carrying a second vector.
void Tracker::predict_all(double dt) {
    for (Track &t : tracks_) {
        t.filter.predict(dt);
        ++t.misses;
    }
}

void Tracker::associate(double dt, std::span<const Detection> dets) {
    pairs_.clear();
    for (std::size_t ti = 0; ti < tracks_.size(); ++ti)
        for (std::size_t di = 0; di < dets.size(); ++di) {
            const Detection &d = dets[di];
            const double error_magnitude =
                tracks_[ti].filter.error_magnitude(d.row, d.col);
            if (error_magnitude <= params_.gate)
                pairs_.push_back({tracks_[ti].hits < params_.confirm_hits,
                                  error_magnitude, ti, di});
        }

    // Confirmed tracks claim first: a newborn beside an established track would
    // otherwise take the detection that belongs to it.
    std::ranges::sort(pairs_);

    det_taken_.assign(dets.size(), 0);
    for (const Pair &p : pairs_) {
        Track &t = tracks_[p.track];
        if (t.misses == 0 || det_taken_[p.det])
            continue;
        det_taken_[p.det] = 1;
        const Detection &d = dets[p.det];
        t.filter.update(dt, d.row, d.col);
        ++t.hits;
        t.misses = 0;
    }
}

void Tracker::spawn(std::span<const Detection> dets) {
    for (std::size_t di = 0; di < dets.size(); ++di)
        if (!det_taken_[di])
            tracks_.push_back(
                {AlphaBetaFilter{dets[di].row, dets[di].col}, next_id_++});
}

void Tracker::cleanup() {
    std::erase_if(
        tracks_, [&](const Track &t) { return t.misses > params_.max_misses; });
}

std::vector<TrackRecord> Tracker::report(FrameId frame) const {
    std::vector<TrackRecord> out;
    for (const Track &t : tracks_) {
        if (t.hits < params_.confirm_hits)
            continue;
        const StateEstimate s = t.filter.state();
        out.push_back({frame, t.id, s.row, s.col, s.v_row, s.v_col});
    }
    return out;
}

std::vector<TrackRecord> Tracker::step(FrameId frame, double dt,
                                       std::span<const Detection> dets) {
    predict_all(dt);
    associate(dt, dets);
    spawn(dets);
    cleanup();
    return report(frame);
}

} // namespace opir
