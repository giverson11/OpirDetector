#include "detect/Cluster.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace opir {

uint32_t label_clusters(Plane<const std::uint8_t> mask, Plane<uint32_t> labels,
                        std::vector<std::size_t> &stack) {
    const std::size_t rows = mask.extent(0), cols = mask.extent(1);
    std::fill(labels.data_handle(), labels.data_handle() + labels.size(),
              uint32_t{0});
    uint32_t label = 0;

    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            if (!mask[r, c] || labels[r, c] != 0)
                continue;

            ++label;
            stack.clear();
            stack.push_back(r * cols + c);
            labels[r, c] = label;

            while (!stack.empty()) {
                const std::size_t idx = stack.back();
                stack.pop_back();
                const std::size_t cr = idx / cols, cc = idx % cols;

                for (int dr = -1; dr <= 1; ++dr)
                    for (int dc = -1; dc <= 1; ++dc) {
                        // Unsigned wraparound puts an out-of-range neighbour
                        // above the extent, so one comparison covers both ends.
                        const std::size_t nr =
                            cr + static_cast<std::size_t>(dr);
                        const std::size_t nc =
                            cc + static_cast<std::size_t>(dc);
                        if (nr >= rows || nc >= cols)
                            continue;
                        if (!mask[nr, nc] || labels[nr, nc] != 0)
                            continue;
                        labels[nr, nc] = label;
                        stack.push_back(nr * cols + nc);
                    }
            }
        }
    }
    return label;
}

std::vector<Detection>
centroid_clusters(FrameSpan px, Plane<const std::uint32_t> labels,
                  std::size_t n_labels, Plane<const double> bg,
                  Plane<const double> sg, const ClusterParams &p,
                  FrameId frame_id) {
    struct Accum {
        double w = 0, wr = 0, wc = 0, peak = 0;
        double sigma_at_peak = 0; // the noise the peak pixel stood out from
        int count = 0;
    };
    std::vector<Accum> acc(n_labels + 1); // index 0 unused

    const std::size_t rows = px.extent(0), cols = px.extent(1);
    for (std::size_t r = 0; r < rows; ++r)
        for (std::size_t c = 0; c < cols; ++c) {
            const std::size_t lbl = static_cast<std::size_t>(labels[r, c]);
            if (lbl == 0)
                continue;

            const double w = std::max(px[r, c] - bg[r, c], 0.0);
            auto &a = acc[lbl];
            a.w += w;
            a.wr += w * static_cast<double>(r);
            a.wc += w * static_cast<double>(c);
            if (w > a.peak) {
                a.peak = w;
                a.sigma_at_peak = sg[r, c];
            }
            ++a.count;
        }

    std::vector<Detection> dets;
    for (std::size_t lbl = 1; lbl <= n_labels; ++lbl) {
        const auto &a = acc[lbl];
        if (a.count < p.min_cluster || a.count > p.max_cluster || a.w <= 0)
            continue;
        // The peak's own local sigma is what it had to stand out from, so it
        // is the denominator that matches how the threshold was set.
        const double snr =
            a.sigma_at_peak > 0.0 ? a.peak / a.sigma_at_peak : 0.0;
        dets.push_back({frame_id, a.wr / a.w, a.wc / a.w, a.peak, snr});
    }
    return dets;
}

} // namespace opir
