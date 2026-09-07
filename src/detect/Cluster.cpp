#include "detect/Cluster.hpp"
#include <cstddef>

namespace opir {

std::vector<Detection>
centroid_clusters(FrameSpan px, std::span<const int32_t> labels,
                  size_t n_labels, std::span<const float> bg,
                  const CfarParams &p, uint32_t frame_id) {
    struct Accum {
        double w = 0, wr = 0, wc = 0, peak = 0;
        int count = 0;
    };
    std::vector<Accum> acc(n_labels + 1); // index 0 unused

    const int rows = static_cast<int>(px.extent(0)),
              cols = static_cast<int>(px.extent(1));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const size_t idx = static_cast<size_t>(r * cols + c);
            const size_t lbl = static_cast<size_t>(labels[idx]);
            if (lbl == 0)
                continue;

            const double w = std::max<double>(static_cast<double>(px[r, c]) -
                                                  static_cast<double>(bg[idx]),
                                              0.0);
            auto &a = acc[lbl];
            a.w += w;
            a.wr += w * r;
            a.wc += w * c;
            a.peak = std::max(a.peak, w);
            ++a.count;
        }

    std::vector<Detection> dets;
    for (size_t lbl = 1; lbl <= n_labels; ++lbl) {
        const auto &a = acc[lbl];
        if (a.count < p.min_cluster || a.count > p.max_cluster || a.w <= 0)
            continue;
        dets.push_back({frame_id, a.wr / a.w, a.wc / a.w, a.peak, /*snr*/ 0.0});
    }
    return dets;
}
} // namespace opir
