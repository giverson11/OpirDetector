#include "Params.hpp"
#include "core/Types.hpp"
#include <vector>

namespace opir {

struct Detection {
    FrameId frame_id;
    double row, col;  // sub-pixel centroid
    double amplitude; // background-subtracted peak
    double snr;
};

int label_clusters(std::span<const uint8_t> mask, int rows, int cols,
                   std::span<int32_t> labels, std::vector<int> &stack);

std::vector<Detection> centroid_clusters(FrameSpan px,
                                         std::span<const int32_t> labels,
                                         int n_labels,
                                         std::span<const float> bg,
                                         const CfarParams &p, FrameId frame_id);
} // namespace opir
