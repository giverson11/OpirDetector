#include "sim/SceneSimulator.hpp"
#include "core/Error.hpp"
#include <algorithm>
#include <cstdint>
#include <format>
#include <limits>
#include <random>

namespace scitec {

namespace {

constexpr double kMaxSaturation =
    static_cast<double>(std::numeric_limits<uint16_t>::max());

constexpr uint16_t quantize(double v) {
    return static_cast<uint16_t>(
        std::clamp(std::round(v), 0.0, kMaxSaturation));
}
} // namespace

SceneSimulator::SceneSimulator(std::size_t rows, std::size_t columns,
                               SceneParams params, uint64_t seed)
    : rows_{rows}, columns_{columns}, rng_{seed},
      fixed_pattern_(rows * columns), params_{params},
      read_noise_{params.mean, params.read_sigma} {

    std::normal_distribution<double> fpn{params.mean, params.fpn_sigma};
    std::ranges::generate(fixed_pattern_, [&] { return fpn(rng_); });
}

void SceneSimulator::add_target(Target target) { targets_.push_back(target); }

void SceneSimulator::render(double t, std::span<uint16_t> out) {
    if (out.size() < rows_ * columns_)
        throw Error(std::format(
            "SceneSimulator needs a buffer of size of at least {} * {}", rows_,
            columns_));

    for (std::size_t r = 0; r < rows_; ++r) {
        for (std::size_t c = 0; c < columns_; ++c) {
            double v = params_.dc_level +
                       params_.row_gradient * static_cast<double>(r) +
                       fixed_pattern_[r * columns_ + c] + read_noise_(rng_);
            for (const auto &target : targets_) {
                double dr = static_cast<double>(r) - target.row(t),
                       dc = static_cast<double>(c) - target.col(t);
                double s2 = target.sigma * target.sigma;
                v += target.amplitude * std::exp(-(dr * dr + dc * dc) / 2 * s2);
            }
            out[r * columns_ + c] = quantize(v);
        }
    }
}

} // namespace scitec