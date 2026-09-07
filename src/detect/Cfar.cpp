#include "detect/Cfar.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace opir {
namespace {
const double kMinVariance = 1e-9;

struct LocalStats {
    double mean, sigma;
};

// Mean and standard deviation of the reference ring around (r,c),
// excluding the guard band so target energy doesn't corrupt the estimate.
LocalStats ring_stats(FrameSpan px, size_t r, size_t c, const CfarParams &p) {
    double sum = 0.0, sum_sq = 0.0;
    int n = 0;

    for (int dr = -p.ref; dr <= p.ref; ++dr) {
        for (int dc = -p.ref; dc <= p.ref; ++dc) {
            const bool in_guard =
                std::abs(dr) <= p.guard && std::abs(dc) <= p.guard;
            if (in_guard)
                continue;

            const double v =
                px[r + static_cast<size_t>(dr), c + static_cast<size_t>(dc)];
            sum += v;
            sum_sq += v * v;
            ++n;
        }
    }

    const double mean = sum / n;
    const double var = std::max(sum_sq / n - mean * mean, kMinVariance);
    return {mean, std::sqrt(var)};
}
} // namespace

void cfar_threshold(FrameSpan px, const CfarParams &p, std::span<uint8_t> mask,
                    std::span<float> bg) {
    const size_t rows = static_cast<size_t>(px.extent(0));
    const size_t cols = static_cast<size_t>(px.extent(1));
    const size_t ref = static_cast<size_t>(p.ref);
    std::ranges::fill(mask, 0);

    // Border pixels are skipped: their reference window would fall outside the
    // frame.
    for (size_t r = ref; r + ref < rows; ++r) {
        for (size_t c = ref; c + ref < cols; ++c) {
            const auto [mean, sigma] = ring_stats(px, r, c, p);
            const double threshold = mean + p.k * sigma;

            bg[r * cols + c] = static_cast<float>(mean);
            mask[r * cols + c] = (px[r, c] > threshold) ? 1 : 0;
        }
    }
}
} // namespace opir
