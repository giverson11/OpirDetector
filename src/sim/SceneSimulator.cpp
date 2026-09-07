#include "sim/SceneSimulator.hpp"
#include "core/Error.hpp"
#include "core/Types.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <random>
#include <sys/types.h>
#include <vector>

namespace opir {

namespace {

constexpr double kMaxSaturation =
    static_cast<double>(std::numeric_limits<Pixel>::max());

constexpr Pixel quantize(double v) {
    return static_cast<Pixel>(std::clamp(std::round(v), 0.0, kMaxSaturation));
}
} // namespace

SceneSimulator::SceneSimulator(std::size_t rows, std::size_t columns,
                               SceneParams params, uint64_t seed)
    : rows_{rows}, columns_{columns}, params_{params}, rng_{seed},
      fixed_pattern_(rows * columns),
      read_noise_{params.mean, params.read_sigma} {

    std::normal_distribution<double> fpn{params.mean, params.fpn_sigma};
    std::ranges::generate(fixed_pattern_, [&] { return fpn(rng_); });
}

void SceneSimulator::add_target(Target target) { targets_.push_back(target); }

void SceneSimulator::render(FrameId frame, std::span<Pixel> out) {
    if (out.size() < rows_ * columns_)
        throw Error(std::format(
            "SceneSimulator needs a buffer of size of at least {} * {}", rows_,
            columns_));
    double t = frame * params_.dt;
    for (std::size_t r = 0; r < rows_; ++r) {
        for (std::size_t c = 0; c < columns_; ++c) {
            double v = params_.dc_level +
                       params_.row_gradient * static_cast<double>(r) +
                       fixed_pattern_[r * columns_ + c] + read_noise_(rng_);

            for (const auto &target : targets_) {
                double dr = static_cast<double>(r) - target.row(t),
                       dc = static_cast<double>(c) - target.col(t);
                double s2 = target.sigma * target.sigma;
                v += target.amplitude *
                     std::exp(-(dr * dr + dc * dc) / (2 * s2));
            }

            out[r * columns_ + c] = quantize(v);
        }
    }
}

std::vector<TruthRecord> SceneSimulator::getTargetRecords(FrameId frame) {
    if (targets_.empty()) {
        throw Error("Simulator has no targets assigned.");
    }

    std::vector<TruthRecord> truths;
    double t = frame * params_.dt;

    for (size_t i = 0; i < targets_.size(); i++) {
        Target target = targets_[i];
        double row = target.row(t);
        double col = target.col(t);

        if (!isTargetInFrame(row, col))
            continue;

        truths.push_back(TruthRecord{.frame_id = frame,
                                     .target_id = static_cast<TargetId>(i),
                                     .row = row,
                                     .col = col,
                                     .amplitude = target.amplitude});
    }
    return truths;
}

bool SceneSimulator::isTargetInFrame(const double row, const double col) {
    return (row >= 0 && row < static_cast<double>(rows_)) &&
           (col >= 0 && col < static_cast<double>(columns_));
}

} // namespace opir
