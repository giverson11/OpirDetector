#include "core/Error.hpp"
#include "sim/SceneSimulator.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <fstream>
#include <memory>
#include <print>
#include <vector>

#ifndef SCENE_DATA_FILE
#error "Scene file must be defined by the build system"
#endif

#ifndef TRUTH_CSV_FILE
#error "Truth csv file must be defined by the build system"
#endif

namespace opir {
namespace {

using Pixel = std::uint16_t;

constexpr size_t kRows = 100;
constexpr size_t kColumns = 100;
constexpr std::uint32_t kFrames = 10;

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
    auto sceneFile = std::ofstream(SCENE_DATA_FILE, std::ios::binary);
    auto truthFile = std::ofstream(TRUTH_CSV_FILE);
    if (!sceneFile)
        throw Error(
            std::format("could not open {} for writing", SCENE_DATA_FILE));

    SceneSimulator simulator(kRows, kColumns, params, kSeed);

    simulator.add_target(Target{.r0 = 24.0,
                                .c0 = 24.0,
                                .r_rate = 0.0,
                                .c_rate = 0.0,
                                .amplitude = 2000.0,
                                .sigma = 3.0});

    std::vector<Pixel> buffer(kRows * kColumns);

    for (std::uint32_t frame = 0; frame < kFrames; frame++) {
        simulator.render(frame, buffer);
        sceneFile.write(reinterpret_cast<const char *>(buffer.data()),
                        static_cast<std::streamsize>(buffer.size()) *
                            static_cast<std::streamsize>(sizeof(Pixel)));

        for (const auto &record : simulator.getTargetRecords(frame)) {
            truthFile << std::format("{}, {}, {}, {}, {}\n", record.frame_id,
                                     record.target_id, record.row, record.col,
                                     record.amplitude);
        }
    }

    std::println("wrote {} frames of {}x{} to {}", kFrames, kRows, kColumns,
                 SCENE_DATA_FILE);
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