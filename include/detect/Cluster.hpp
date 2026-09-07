#pragma once

#include "core/Types.hpp"

#include <cstdint>
#include <span>
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

int label_clusters(std::span<const uint8_t> mask, int rows, int cols,
                   std::span<int32_t> labels, std::vector<int> &stack);

std::vector<Detection>
centroid_clusters(FrameSpan px, std::span<const int32_t> labels, int n_labels,
                  std::span<const float> bg, const ClusterParams &p,
                  FrameId frame_id);
} // namespace opir
