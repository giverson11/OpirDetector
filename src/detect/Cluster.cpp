#include "detect/Cluster.hpp"
#include <cstddef>

namespace opir {
int label_clusters(std::span<const uint8_t> mask, int rows, int cols,
                   std::span<int32_t> labels, std::vector<int> &stack) {
    std::ranges::fill(labels, 0);
    int next = 0;

    for (size_t i = 0; i < rows * cols; ++i) {
        if (!mask[i] || labels[i] != 0)
            continue;

        ++next;
        stack.clear();
        stack.push_back(static_cast<int>(i));
        labels[i] = next;

        while (!stack.empty()) {
            const int idx = stack.back();
            stack.pop_back();
            const int r = idx / cols, c = idx % cols;

            for (int dr = -1; dr <= 1; ++dr)
                for (int dc = -1; dc <= 1; ++dc) {
                    const int nr = r + dr, nc = c + dc;
                    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols)
                        continue;
                    const size_t n = static_cast<size_t>(nr * cols + nc);
                    if (!mask[n] || labels[n] != 0)
                        continue;
                    labels[n] = next;
                    stack.push_back(static_cast<int>(n));
                }
        }
    }
    return next;
}

std::vector<Detection>
centroid_clusters(FrameSpan px, std::span<const int32_t> labels,
                  size_t n_labels, std::span<const float> bg,
                  const ClusterParams &p, uint32_t frame_id) {
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
