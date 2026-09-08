#include "detect/Cluster.hpp"
#include "core/Types.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace opir {
namespace {

/// A mask drawn as ASCII art, so a test reads as the shape it is about:
/// '#' is a lit pixel, anything else is background.
struct Mask {
    int rows = 0, cols = 0;
    std::vector<uint8_t> bits;

    std::size_t nrows() const { return static_cast<std::size_t>(rows); }
    std::size_t ncols() const { return static_cast<std::size_t>(cols); }

    static Mask from(std::initializer_list<std::string_view> art) {
        Mask m;
        m.rows = static_cast<int>(art.size());
        m.cols = static_cast<int>(art.begin()->size());
        for (const std::string_view row : art)
            for (const char ch : row)
                m.bits.push_back(ch == '#' ? uint8_t{1} : uint8_t{0});
        return m;
    }
    std::size_t size() const { return bits.size(); }
};

/// Labels a mask and returns both the label image and how many were found.
struct Labelled {
    std::uint32_t count = 0;
    std::vector<std::uint32_t> labels;

    std::uint32_t at(const Mask &m, int r, int c) const {
        return labels[static_cast<std::size_t>(r * m.cols + c)];
    }
    /// The distinct non-zero labels present, in ascending order.
    std::vector<std::uint32_t> distinct() const {
        std::vector<std::uint32_t> ids;
        for (const std::uint32_t l : labels)
            if (l != 0 && std::ranges::find(ids, l) == ids.end())
                ids.push_back(l);
        std::ranges::sort(ids);
        return ids;
    }
};

Labelled label(const Mask &m) {
    Labelled out{0, std::vector<std::uint32_t>(m.size(), 0)};
    std::vector<std::size_t> stack;
    out.count = label_clusters(
        Plane<const std::uint8_t>{m.bits.data(), m.nrows(), m.ncols()},
        Plane<std::uint32_t>{out.labels.data(), m.nrows(), m.ncols()}, stack);
    return out;
}

// ---------------------------------------------------------------------------
// label_clusters
// ---------------------------------------------------------------------------

/// Ids are dense from 1 in raster order so a caller can size a table by the
/// count; corners must not be skipped; unlit pixels stay 0.
TEST(LabelClusters, NumbersEachBlobDenselyFromOneIncludingCorners) {
    const Mask m = Mask::from({"#...#", ".....", "..#..", ".....", "#...#"});

    const Labelled out = label(m);

    EXPECT_EQ(out.count, 5u);
    EXPECT_EQ(out.distinct(), (std::vector<std::uint32_t>{1, 2, 3, 4, 5}));
    EXPECT_EQ(out.at(m, 0, 0), 1u);
    EXPECT_EQ(out.at(m, 0, 4), 2u);
    EXPECT_EQ(out.at(m, 2, 2), 3u);
    EXPECT_EQ(out.at(m, 4, 4), 5u);
    EXPECT_EQ(out.at(m, 1, 1), 0u) << "an unlit pixel carries no label";
}

/// Connectivity is 8-way, so a diagonal touch is one cluster and not two, and
/// the flood fill has to walk round corners rather than fill a rectangle.
TEST(LabelClusters, JoinsThroughDiagonalsAndAroundCorners) {
    const Mask vee = Mask::from({"#...#", ".#.#.", "..#.."});
    const Labelled joined = label(vee);
    EXPECT_EQ(joined.count, 1u);
    EXPECT_EQ(joined.at(vee, 0, 0), joined.at(vee, 0, 4))
        << "the two arms meet only at the bottom of the V";

    const Mask apart = Mask::from({"#....", "..#..", "....."});
    EXPECT_EQ(label(apart).count, 2u) << "a knight's move is not adjacency";
}

/// The stack is scratch space owned by the caller so it can be reused; reusing
/// it must not leak state from one call into the next.
TEST(LabelClusters, GivesTheSameAnswerWhenTheScratchStackIsReused) {
    const Mask big = Mask::from({"###..", "###..", "....."});
    const Mask small = Mask::from({".....", "..#..", "....."});

    std::vector<std::size_t> stack;
    std::vector<std::uint32_t> a(big.size()), b(small.size());
    const auto run = [&stack](const Mask &m, std::vector<std::uint32_t> &out) {
        return label_clusters(
            Plane<const std::uint8_t>{m.bits.data(), m.nrows(), m.ncols()},
            Plane<std::uint32_t>{out.data(), m.nrows(), m.ncols()}, stack);
    };

    EXPECT_EQ(run(big, a), 1u);
    EXPECT_EQ(run(small, b), 1u);
    EXPECT_EQ(run(big, a), 1u)
        << "a stack carrying leftovers would corrupt the next frame";
}

// ---------------------------------------------------------------------------
// centroid_clusters
// ---------------------------------------------------------------------------

/// A scene built from a mask: every lit pixel gets `signal` counts above a flat
/// background, so the cluster's centroid is the mask's own centre of mass.
struct Scene {
    Mask mask;
    std::vector<Pixel> px;
    std::vector<double> bg;
    std::vector<double> sg;

    static Scene from(const Mask &m, Pixel background, Pixel signal,
                      double sigma = 1.0) {
        Scene s{m, std::vector<Pixel>(m.size(), background),
                std::vector<double>(m.size(), static_cast<double>(background)),
                std::vector<double>(m.size(), sigma)};
        for (std::size_t i = 0; i < m.size(); ++i)
            if (m.bits[i])
                s.px[i] = static_cast<Pixel>(background + signal);
        return s;
    }
    FrameSpan span() const {
        return FrameSpan{px.data(), mask.nrows(), mask.ncols()};
    }
    Plane<const double> bg_plane() const {
        return Plane<const double>{bg.data(), mask.nrows(), mask.ncols()};
    }
    Plane<const double> sg_plane() const {
        return Plane<const double>{sg.data(), mask.nrows(), mask.ncols()};
    }
};

std::vector<Detection> detect(const Scene &s, const ClusterParams &p,
                              FrameId frame_id = 0) {
    const Labelled l = label(s.mask);
    return centroid_clusters(s.span(),
                             Plane<const std::uint32_t>{l.labels.data(),
                                                        s.mask.nrows(),
                                                        s.mask.ncols()},
                             l.count, s.bg_plane(), s.sg_plane(), p, frame_id);
}

TEST(CentroidClusters, PlacesAClusterAtItsCentreOfMassAndStampsTheFrame) {
    // A 2x2 block spanning rows 1-2 and columns 1-2 centres on (1.5, 1.5).
    const Scene block =
        Scene::from(Mask::from({"....", ".##.", ".##.", "...."}), 1000, 500);

    const std::vector<Detection> dets = detect(block, ClusterParams{}, 7);

    ASSERT_EQ(dets.size(), 1u);
    EXPECT_DOUBLE_EQ(dets[0].row, 1.5);
    EXPECT_DOUBLE_EQ(dets[0].col, 1.5);
    EXPECT_EQ(dets[0].frame_id, 7u);

    // An L of equal-weight pixels at (1,1), (1,2) and (2,2): the row and column
    // centroids differ, which a symmetric blob could never show.
    const Scene ell =
        Scene::from(Mask::from({"....", ".##.", "..#.", "...."}), 1000, 500);

    const std::vector<Detection> bent = detect(ell, ClusterParams{});

    ASSERT_EQ(bent.size(), 1u);
    EXPECT_NEAR(bent[0].row, 4.0 / 3.0, 1e-12) << "(1 + 1 + 2) / 3";
    EXPECT_NEAR(bent[0].col, 5.0 / 3.0, 1e-12) << "(1 + 2 + 2) / 3";
}

/// The size bounds are what separate a target from a hot pixel at one end and
/// from a cloud edge at the other.
TEST(CentroidClusters, RejectsClustersOutsideTheSizeBounds) {
    // Three clusters, of 1, 3 and 4 pixels.
    const Scene s = Scene::from(
        Mask::from({"#..###.", ".......", "##.....", "##....."}), 1000, 500);
    ClusterParams p{};
    p.min_cluster = 2;
    p.max_cluster = 4;

    ASSERT_EQ(detect(s, p).size(), 2u)
        << "only the lone pixel is out of bounds";

    p.max_cluster = 3;
    EXPECT_EQ(detect(s, p).size(), 1u) << "the 4-pixel block is now too large";

    p.min_cluster = 4;
    p.max_cluster = 25;
    EXPECT_EQ(detect(s, p).size(), 1u) << "now only the 4-pixel block survives";
}

/// Amplitude is the brightest pixel above its background and SNR is that
/// divided by the sigma at that pixel. Weights are clamped at zero, so a
/// cluster sitting entirely at or below its background carries no weight and
/// produces no detection at all.
TEST(CentroidClusters, ReportsPeakAndSnrAndSkipsAClusterBelowItsBackground) {
    Scene s = Scene::from(Mask::from({".....", ".###.", ".....", "##..."}),
                          1000, 200, /*sigma=*/100.0);
    s.px[1 * 5 + 2] = 1900;                     // the peak: 900 above bg
    s.bg[1 * 5 + 1] = 1300.0;                   // left neighbour under bg
    s.bg[1 * 5 + 3] = 1100.0;                   // right neighbour 100 above
    s.bg[3 * 5 + 0] = s.bg[3 * 5 + 1] = 2000.0; // bottom cluster under bg

    const std::vector<Detection> dets = detect(s, ClusterParams{});

    ASSERT_EQ(dets.size(), 1u) << "the cluster with no weight is skipped";
    EXPECT_DOUBLE_EQ(dets[0].row, 1.0) << "the survivor is the top one";
    EXPECT_DOUBLE_EQ(dets[0].col, 2.1)
        << "(900 * 2 + 0 * 1 + 100 * 3) / 1000: a pixel under its background "
           "gets zero weight, not negative";
    EXPECT_DOUBLE_EQ(dets[0].amplitude, 900.0) << "peak minus background";
    EXPECT_DOUBLE_EQ(dets[0].snr, 9.0)
        << "the peak divided by the sigma at the peak pixel";
}

} // namespace
} // namespace opir
