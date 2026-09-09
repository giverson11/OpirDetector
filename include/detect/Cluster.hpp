#pragma once

#include "core/Types.hpp"
#include "detect/Cfar.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace opir {

///
/// Tuning for which connected components survive as detections: anything
/// smaller is noise, anything larger is not a point target.
///
struct ClusterParams {
    int min_cluster = 2;
    int max_cluster = 25;
};

struct Detection {
    FrameId frame_id;
    double row, col;  // sub-pixel centroid
    double amplitude; // background-subtracted peak
    double snr;
};

///
/// Labels 8-connected runs of set mask pixels, numbering them 1..n. The shape
/// comes from the mask, and `labels` must have the same extents.
///
/// @param mask
/// @param labels cleared in full, then written
/// @param stack caller-owned scratch space, reused across calls
/// @return how many components were found
///
std::uint32_t label_clusters(Plane<const std::uint8_t> mask,
                             Plane<std::uint32_t> labels,
                             std::vector<std::size_t> &stack);

///
/// Accumulates the values from each cluster and determines the intensity
/// weighted average of each pixel. Using that to find the centroid of the
/// cluster, its peak value
///
/// @param px
/// @param labels
/// @param n_labels
/// @param bg local mean, from cfar_threshold
/// @param sg local sigma, from cfar_threshold; sets each detection's snr
/// @param p
/// @param frame_id
/// @return
///
std::vector<Detection>
centroid_clusters(FrameSpan px, Plane<const std::uint32_t> labels,
                  std::size_t n_labels, Plane<const double> bg,
                  Plane<const double> sg, const ClusterParams &p,
                  FrameId frame_id);

} // namespace opir
