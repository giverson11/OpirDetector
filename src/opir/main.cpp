#include "core/Types.hpp"
#include "detect/Cfar.hpp"
#include "detect/Cluster.hpp"
#include "filter/Tracker.hpp"
#include "frame/FrameReader.hpp"
#include "sim/TruthWriter.hpp"
#include <cstddef>
#include <cstdio>
#include <exception>
#include <format>
#include <print>
#include <string_view>
#include <sys/types.h>
#include <vector>

#ifndef SCENE_DATA_FILE
#error "Scene file must be defined by the build system"
#endif

#ifndef TRUTH_CSV_FILE
#error "Truth csv file must be defined by the build system"
#endif

namespace opir {
namespace {

int run(std::vector<std::string_view> arguments) {

    FrameReader frameData{SCENE_DATA_FILE};

    auto data = frameData.next();
    if (!data) {
        std::println(stderr, "{}", data.error());
        return 2;
    }
    const std::size_t rows = data->px.extent(0), cols = data->px.extent(1);

    std::vector<std::uint8_t> mask(rows * cols);
    std::vector<double> bg(rows * cols);
    std::vector<double> sg(rows * cols);
    std::vector<std::uint32_t> labels(rows * cols);
    std::vector<std::size_t> stack{};

    cfar_threshold(data->px, CfarParams{},
                   Plane<std::uint8_t>{mask.data(), rows, cols},
                   Plane<double>{bg.data(), rows, cols},
                   Plane<double>{sg.data(), rows, cols});

    auto labelCount =
        label_clusters(Plane<std::uint8_t>{mask.data(), rows, cols},
                       Plane<std::uint32_t>{labels.data(), rows, cols}, stack);

    auto dets = centroid_clusters(
        data->px, Plane<std::uint32_t>{labels.data(), rows, cols}, labelCount,
        Plane<const double>{bg.data(), rows, cols},
        Plane<const double>{sg.data(), rows, cols}, ClusterParams{}, data->id);

    Tracker track{TrackParams{}};
    track.step(data->id, data->t, dets);

    return 0;
}
} // namespace
} // namespace opir
int main(int argc, char **argv) {
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);
    try {
        return opir::run(arguments);
    } catch (const std::exception &error) {
        return 1;
    }
    return 0;
}
