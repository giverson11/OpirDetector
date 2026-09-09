#include "core/ParseError.hpp"
#include "core/Types.hpp"
#include "detect/Cfar.hpp"
#include "detect/Cluster.hpp"
#include "filter/Tracker.hpp"
#include "frame/FrameReader.hpp"
#include "sim/TruthWriter.hpp"
#include <alloca.h>
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

    // The tracker, and the scratch these stages write into, live across the
    // whole stream: a track needs TrackParams::confirm_hits frames of evidence
    // before it is reported.
    Tracker track{TrackParams{}};
    std::vector<std::uint8_t> mask;
    std::vector<double> bg, sg;
    std::vector<std::uint32_t> labels;
    std::vector<std::size_t> stack;

    auto data = frameData.next();
    double prev_t = data ? data->t : 0;

    for (; data; data = frameData.next()) {

        const std::size_t rows = data->px.extent(0), cols = data->px.extent(1);

        mask.assign(rows * cols, 0);
        bg.assign(rows * cols, 0.0);
        sg.assign(rows * cols, 0.0);
        labels.assign(rows * cols, 0);
        stack.clear();

        // The guard band has to clear the PSF, or a target's own skirt lands
        // in its reference ring and inflates the sigma it is measured against.
        cfar_threshold(data->px, CfarParams{.guard = 9, .ref = 14},
                       Plane<std::uint8_t>{mask.data(), rows, cols},
                       Plane<double>{bg.data(), rows, cols},
                       Plane<double>{sg.data(), rows, cols});

        auto labelCount = label_clusters(
            Plane<std::uint8_t>{mask.data(), rows, cols},
            Plane<std::uint32_t>{labels.data(), rows, cols}, stack);

        auto dets = centroid_clusters(
            data->px, Plane<std::uint32_t>{labels.data(), rows, cols},
            labelCount, Plane<const double>{bg.data(), rows, cols},
            Plane<const double>{sg.data(), rows, cols},
            // A sigma = 3 PSF at this amplitude clears the threshold out to
            // ~6.6 px, so a single point target lands ~140 px of mask. The
            // default cap of 25 is sized for a much tighter PSF.
            ClusterParams{.min_cluster = 2, .max_cluster = 200}, data->id);

        // step() wants the gap since the previous frame, not the timestamp.
        const double dt = data->t - prev_t;
        prev_t = data->t;
        auto report = track.step(data->id, dt, dets);

        for (auto &r : report) {
            std::println("(Frame: {}, Item: {}) => {}, {}, {}, {} ", r.frame_id,
                         r.track_id, r.row, r.col, r.v_row, r.v_col);
        }
    }
    if (data.error() == ParseError::EndOfStream)
        return 0;
    std::println(stderr, "{}", data.error());
    return 2;
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
