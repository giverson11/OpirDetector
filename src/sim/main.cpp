#include "core/Error.hpp"
#include "core/Types.hpp"
#include "frame/FrameWriter.hpp"
#include "sim/SceneSimulator.hpp"
#include "sim/TruthWriter.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <fstream>
#include <print>

#ifndef SCENE_DATA_FILE
#error "Scene file must be defined by the build system"
#endif

#ifndef TRUTH_CSV_FILE
#error "Truth csv file must be defined by the build system"
#endif

namespace opir {
namespace {

constexpr size_t kRows = 100;
constexpr size_t kColumns = 100;
constexpr FrameId kLastFrameId = 10;

constexpr uint64_t kSeed = 42;

/// A representative detector: a 10000-count pedestal, a gentle top-to-bottom
/// gradient, and both noise sources live.
constexpr SceneParams params = {.mean = 0.0,
                                .fpn_sigma = 15.0,
                                .read_sigma = 8.0,
                                .dc_level = 10000.0,
                                .row_gradient = 3.0,
                                .dt = 0.3};

int run() {
    FrameWriter sceneWriter{SCENE_DATA_FILE, kRows, kColumns};
    TruthWriter truthWriter = TruthWriter{TRUTH_CSV_FILE};

    SceneSimulator simulator(kRows, kColumns, params, kSeed);

    simulator.add_target(Target{.r0 = 24.0,
                                .c0 = 24.0,
                                .r_rate = 0.0,
                                .c_rate = 0.0,
                                .amplitude = 2000.0,
                                .sigma = 3.0});

    std::vector<Pixel> buffer(kRows * kColumns);

    for (FrameId frame = 0; frame < kLastFrameId; frame++) {
        simulator.render(frame, buffer);
        sceneWriter.write_frame(frame, frame * params.dt, buffer);

        truthWriter.write_truth(simulator.getTargetRecords(frame));
    }

    std::println("wrote {} frames of {}x{} to {}", kLastFrameId, kRows,
                 kColumns, SCENE_DATA_FILE);
    return 0;
}
} // namespace
} // namespace opir

int main() {
    try {
        return opir::run();
    } catch (const std::exception &error) {
        std::println(stderr, "scene_gen: {}", error.what());
        return 1;
    }
}
