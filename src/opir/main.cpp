#include "core/ParseError.hpp"
#include "core/Types.hpp"
#include "detect/Cfar.hpp"
#include "detect/Cluster.hpp"
#include "eval/Score.hpp"
#include "filter/Tracker.hpp"
#include "frame/FrameReader.hpp"
#include "sim/TruthTable.hpp"
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

///
/// The run in one table: how each target was followed, and what was lost.
///
/// Radial error is a magnitude, so its mean sits above zero even for a
/// tracker with no bias at all; the signed row and column means are where a
/// filter's lag actually shows.
///
void report_summary(const RunSummary &summary) {
    std::println("");
    std::println("{} frames scored, {} misses, {} false alarms", summary.frames,
                 summary.total_misses, summary.total_false_alarms);
    std::println("{:>6}  {:>7}  {:>7}  {:>6}  {:>17}  {:>17}  {:>17}", "target",
                 "present", "tracked", "missed", "radial px", "d_row px",
                 "d_col px");

    for (const TargetStats &t : summary.targets) {
        std::println("{:>6}  {:>7}  {:>7}  {:>6}  {:>8.3f} +/-{:>6.3f}"
                     "  {:>+8.3f} +/-{:>6.3f}  {:>+8.3f} +/-{:>6.3f}",
                     t.target_id, t.frames_present, t.frames_tracked, t.misses,
                     t.radial.mean(), t.radial.stddev(), t.d_row.mean(),
                     t.d_row.stddev(), t.d_col.mean(), t.d_col.stddev());
    }

    // The first frames of a run are structurally missed: a track is not
    // reported until it has TrackParams::confirm_hits of evidence behind it.
    for (const TargetStats &t : summary.targets) {
        if (t.first_tracked) {
            std::println("target {} acquired at frame {}", t.target_id,
                         *t.first_tracked);
        } else {
            std::println("target {} was never tracked", t.target_id);
        }
    }
}

int run(std::vector<std::string_view> arguments) {

    FrameReader frameData{SCENE_DATA_FILE};
    auto truthData = TruthTable::load(TRUTH_CSV_FILE);

    if (!truthData) {
        std::println(stderr, "truth: {}", truthData.error());
        return 2;
    }
    Scorer scorer{ScoreParams{}};

    // The tracker, and the scratch these stages write into, live across the
    // whole stream: a track needs TrackParams::confirm_hits frames of
    // evidence before it is reported.
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

        const FrameScore score =
            scorer.add(data->id, truthData->view_at(data->id), report);

        std::println("frame {:>4}  matched {}/{}  missed {}  false {}",
                     score.frame_id, score.matched.size(),
                     score.matched.size() + score.missed.size(),
                     score.missed.size(), score.false_alarms.size());
        for (const Match &m : score.matched) {
            std::println("    target {} <- track {}   d = {:+.3f}, {:+.3f}"
                         "   |d| = {:.3f}",
                         m.target_id, m.track_id, m.d_row, m.d_col, m.distance);
        }
        for (const TargetId missed : score.missed) {
            std::println("    target {} MISSED", missed);
        }
        for (const TrackId spurious : score.false_alarms) {
            std::println("    track {} matches no truth", spurious);
        }
    }

    report_summary(scorer.summary());
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
