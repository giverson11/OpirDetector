#include "core/Error.hpp"
#include "core/Types.hpp"
#include "frame/FrameWriter.hpp"
#include "sim/SceneSimulator.hpp"
#include "sim/TruthWriter.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <print>

#ifndef SCENE_DATA_FILE
#error "Scene file must be defined by the build system"
#endif

#ifndef TRUTH_CSV_FILE
#error "Truth csv file must be defined by the build system"
#endif

namespace opir {
namespace {

constexpr size_t kRows = 1000;
constexpr size_t kColumns = 1000;
constexpr FrameId kLastFrameId = 100;

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
    // Targets have to clear the detector's CFAR reference window, not just each
    // other's PSF. That window is a square annulus, so the separation that
    // counts is max(|dr|, |dc|), and it has to exceed the window's outer
    // half-width plus the PSF skirt. A 2x2 layout 44 px apart clears it with
    // room to spare; a diagonal line would not fit four in a 100x100 frame.
    for (int i = 0; i < 4; ++i) {
        simulator.add_target(Target{.r0 = 24.0 + 44.0 * (i / 2),
                                    .c0 = 24.0 + 44.0 * (i % 2),
                                    .r_rate = 2.0 * i,
                                    .c_rate = 2.0 + 1.3 * i,
                                    .amplitude = 2000.0,
                                    .sigma = 3.0});
    }
    std::vector<Pixel> buffer(kRows * kColumns);

    for (FrameId frame = 0; frame < kLastFrameId; frame++) {
        simulator.render(frame, Plane<Pixel>{buffer.data(), kRows, kColumns});
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
