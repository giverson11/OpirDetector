#pragma once

#include "core/Types.hpp"
#include "detect/Cluster.hpp"
#include "filter/AlphaBeta.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace opir {

/// Tuning for track life: how close a detection has to fall to be claimed, and
/// how many frames of evidence open and close a track.
struct TrackParams {
    double gate = 4.0;    // max pixels from a prediction to associate
    int confirm_hits = 3; // hits before a track is reported
    int max_misses = 3;   // consecutive coasts before a track is dropped
};

struct Track {
    AlphaBetaFilter filter;
    int hits = 1;
    int misses = 0; // 0 means claimed this frame; see Tracker::predict_all
};

/// A confirmed track's estimate at one frame. It carries no identity, so
/// consecutive frames cannot yet be stitched into a trajectory.
struct TrackRecord {
    FrameId frame_id;
    double row, col;
    double v_row, v_col;
};

/// Carries tracks from frame to frame. Association is greedy nearest
/// neighbour: every in-gate track and detection pair is scored, and pairs are
/// claimed best-first until nothing within the gate is left.
class Tracker {
    /// The member order is the sort order: confirmed tracks first, then
    /// cheapest first, with the indices breaking exact ties.
    struct Pair {
        bool tentative;
        double cost;
        std::size_t track, det;
        friend auto operator<=>(const Pair &, const Pair &) = default;
    };

    TrackParams params_;
    std::vector<Track> tracks_;

    // Scratch reused across frames, like the cluster stack.
    std::vector<Pair> pairs_;
    std::vector<char> det_taken_;

    void predict_all(double dt);
    void associate(double dt, std::span<const Detection> dets);
    void spawn(std::span<const Detection> dets);
    void cleanup();
    std::vector<TrackRecord> report(FrameId frame) const;

  public:
    explicit Tracker(TrackParams params = {}) : params_{params} {}

    ///
    /// Advances every track by dt, claims detections, births and buries
    /// tracks, and reports the confirmed ones.
    ///
    /// \param frame
    /// \param dt seconds since the previous call
    /// \param dets this frame's output from centroid_clusters
    /// \return one record per confirmed track, whether or not it was hit
    ///
    std::vector<TrackRecord> step(FrameId frame, double dt,
                                  std::span<const Detection> dets);

    std::span<const Track> tracks() const { return tracks_; }
};

} // namespace opir
