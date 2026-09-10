#include "sim/TruthTable.hpp"
#include "core/ParseError.hpp"
#include "core/Types.hpp"
#include "sim/SceneSimulator.hpp"
#include "sim/TruthWriter.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace opir {
namespace {

TruthRecord record(FrameId frame, TargetId target, double row, double col,
                   double amplitude) {
    return TruthRecord{.frame_id = frame,
                       .target_id = target,
                       .row = row,
                       .col = col,
                       .amplitude = amplitude};
}

/// A path under the temp directory holding exactly `text`, so a test can put a
/// hand-written table in front of the parser.
std::filesystem::path scratch(std::string_view name, std::string_view text) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out{path};
    out << text;
    return path;
}

/// Field-by-field, so a failure says which column moved rather than that two
/// structs differ.
void expect_same(const TruthRecord &got, const TruthRecord &want) {
    EXPECT_EQ(got.frame_id, want.frame_id);
    EXPECT_EQ(got.target_id, want.target_id);
    EXPECT_EQ(got.row, want.row);
    EXPECT_EQ(got.col, want.col);
    EXPECT_EQ(got.amplitude, want.amplitude);
}

/// The pairing that matters: whatever TruthWriter emits, this reads back
/// unchanged. Exact comparison is the point -- std::format writes the shortest
/// round-trip form of a double and from_chars recovers exactly it, so anything
/// less than equality here would be a lost bit. This is also the only guard on
/// the wire format itself: a header line or a changed separator would surface
/// here as a parse failure.
TEST(TruthTable, RoundTripsEveryFieldTruthWriterWrites) {
    const std::vector<TruthRecord> written{
        record(0, 0, 24.0, 24.0, 2000.0), record(0, 1, 0.1, -3.5, 1e20),
        record(1, 0, 10.5, 20.25, 1500.0), record(1, 1, -1.5, 999.5, 0.0)};

    const auto path = std::filesystem::temp_directory_path() /
                      "opir_truth_roundtrip.csv";
    {
        TruthWriter writer{path};
        writer.write_truth(written);
    }

    const auto table = TruthTable::load(path);
    ASSERT_TRUE(table.has_value()) << to_string(table.error());
    ASSERT_EQ(table->view_all().size(), written.size());
    for (std::size_t i = 0; i < written.size(); ++i) {
        expect_same(table->view_all()[i], written[i]);
    }
    std::filesystem::remove(path);
}

/// The whole reason for the index. Counts are read off the file rather than
/// assumed, because the simulator drops targets that have left the frame; a
/// frame everything has left is a hole that must come back empty rather than
/// spilling the next frame's rows; and a frame past the end behaves the same
/// way so a read loop can outrun truth.
TEST(TruthTable, GroupsRaggedFrameCountsAndReturnsEmptySpansForHoles) {
    const auto path = scratch("opir_truth_index.csv", "0, 2, 3, 3, 300\n"
                                                      "0, 0, 1, 1, 100\n"
                                                      "0, 1, 2, 2, 200\n"
                                                      "1, 2, 5, 5, 500\n"
                                                      "3, 0, 6, 6, 600\n");

    const auto table = TruthTable::load(path);
    ASSERT_TRUE(table.has_value()) << to_string(table.error());

    EXPECT_EQ(table->frame_count(), 4u);
    ASSERT_EQ(table->view_at(0).size(), 3u);
    ASSERT_EQ(table->view_at(1).size(), 1u);
    EXPECT_TRUE(table->view_at(2).empty()) << "frame 2 is a hole in the table";
    ASSERT_EQ(table->view_at(3).size(), 1u);
    EXPECT_TRUE(table->view_at(4).empty()) << "past the end";
    EXPECT_TRUE(table->view_at(9999).empty());

    // The file listed frame 0 out of target order; the index needs each frame
    // contiguous and sorted, so loading has to put it right.
    EXPECT_EQ(table->view_at(0)[0].target_id, 0u);
    EXPECT_EQ(table->view_at(0)[2].target_id, 2u);
    expect_same(table->view_at(1)[0], record(1, 2, 5.0, 5.0, 500.0));
    std::filesystem::remove(path);
}

/// A line that is not five numbers is refused rather than half-read. Silently
/// keeping the fields that did parse would score a run against truth that is
/// partly invented. The last case is the ceiling on the frame id: the index is
/// one slot per frame, so a corrupt id would size an allocation from the file.
TEST(TruthTable, ReportsMalformedRecordRatherThanReadingPartOfALine) {
    for (const std::string_view bad : {
             "0, 0, 1, 1\n",             // a field short
             "0, 0, 1, 1, 100, 7\n",     // a field long
             "0, 0, 1, 1, abc\n",        // not a number
             "0 0, 1, 1, 100\n",         // a missing separator
             "frame, target, r, c, a\n", // a header line
             "0, 0, 1, 1, 100x\n",       // trailing garbage on a good field
             "-1, 0, 1, 1, 100\n",       // a negative frame id
             "4000000000, 0, 2, 2, 2\n", // a frame id past the ceiling
         }) {
        const auto path = scratch("opir_truth_bad.csv", bad);
        const auto table = TruthTable::load(path);
        ASSERT_FALSE(table.has_value()) << "accepted: " << bad;
        EXPECT_EQ(table.error(), ParseError::MalformedRecord) << bad;
        std::filesystem::remove(path);
    }
}

} // namespace
} // namespace opir
