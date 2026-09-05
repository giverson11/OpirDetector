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
constexpr int kFrames = 10;

constexpr uint64_t kSeed = 42;

/// A representative detector: a 10000-count pedestal, a gentle top-to-bottom
/// gradient, and both noise sources live.
constexpr SceneParams params = {.mean = 0.0,
                                .fpn_sigma = 15.0,
                                .read_sigma = 8.0,
                                .dc_level = 10000.0,
                                .row_gradient = 3.0};

int run() {
    auto sceneFile = std::ofstream(SCENE_DATA_FILE, std::ios::binary);
    if (!sceneFile)
        throw Error(
            std::format("could not open {} for writing", SCENE_DATA_FILE));

    SceneSimulator simulator(kRows, kColumns, params, kSeed);

    std::vector<Pixel> buffer(kRows * kColumns);

    for (int i = 0; i < kFrames; i++) {
        double t = i * 0.3;
        simulator.render(t, buffer);
        sceneFile.write(reinterpret_cast<const char *>(buffer.data()),
                        static_cast<std::streamsize>(buffer.size()) *
                            static_cast<std::streamsize>(sizeof(Pixel)));
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