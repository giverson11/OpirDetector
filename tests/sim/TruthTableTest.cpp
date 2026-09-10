#include "sim/TruthTable.hpp"
#include "core/Error.hpp"
#include "core/ParseError.hpp"
#include "core/Types.hpp"
#include "sim/SceneSimulator.hpp"
#include "sim/TruthWriter.hpp"

#include "support/TempFile.hpp"

#include <gtest/gtest.h>

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

void write_text(const test::TempFile &file, std::string_view text) {
    std::ofstream out{file.path()};
    out << text;
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

TEST(TruthTableLoad, ThrowsWhenThePathCannotBeOpened) {
    EXPECT_THROW((void)TruthTable::load(test::unopenable_path()), Error);
}

/// The pairing that matters: whatever TruthWriter emits, this reads back
/// unchanged. Exact comparison is the point -- std::format writes the shortest
/// round-trip form of a double and from_chars recovers exactly it, so anything
/// less than equality here would be a lost bit.
TEST(TruthTableLoad, RoundTripsEveryFieldTruthWriterWrites) {
    const std::vector<TruthRecord> written{
        record(0, 0, 24.0, 24.0, 2000.0), record(0, 1, 0.1, -3.5, 1e20),
        record(1, 0, 10.5, 20.25, 1500.0), record(1, 1, -1.5, 999.5, 0.0)};

    test::TempFile file{".csv"};
    {
        TruthWriter writer{file.path()};
        writer.write_truth(written);
    }

    const auto table = TruthTable::load(file.path());
    ASSERT_TRUE(table.has_value()) << to_string(table.error());
    ASSERT_EQ(table->view_all().size(), written.size());
    for (std::size_t i = 0; i < written.size(); ++i) {
        expect_same(table->view_all()[i], written[i]);
    }
}

/// The whole reason for the index: a frame's records come back together, and
/// the count is read off the file rather than assumed, because the simulator
/// drops targets that have left the frame.
TEST(TruthTableViewAt, GroupsRaggedFrameCountsAndOrdersByTarget) {
    test::TempFile file{".csv"};
    write_text(file, "0, 0, 1, 1, 100\n"
                     "0, 1, 2, 2, 200\n"
                     "0, 2, 3, 3, 300\n"
                     "1, 2, 5, 5, 500\n"
                     "2, 0, 6, 6, 600\n");

    const auto table = TruthTable::load(file.path());
    ASSERT_TRUE(table.has_value()) << to_string(table.error());

    EXPECT_EQ(table->frame_count(), 3u);
    ASSERT_EQ(table->view_at(0).size(), 3u);
    ASSERT_EQ(table->view_at(1).size(), 1u);
    ASSERT_EQ(table->view_at(2).size(), 1u);

    EXPECT_EQ(table->view_at(0)[0].target_id, 0u);
    EXPECT_EQ(table->view_at(0)[2].target_id, 2u);
    expect_same(table->view_at(1)[0], record(1, 2, 5.0, 5.0, 500.0));
}

/// A frame every target has left is written as nothing at all, and a scorer
/// asking about it must get an empty span rather than the next frame's rows.
/// Frames past the end behave the same way, so a read loop can outrun truth.
TEST(TruthTableViewAt, ReturnsAnEmptySpanForAGapAndForAFrameBeyondTheEnd) {
    test::TempFile file{".csv"};
    write_text(file, "0, 0, 1, 1, 100\n"
                     "3, 0, 4, 4, 400\n");

    const auto table = TruthTable::load(file.path());
    ASSERT_TRUE(table.has_value()) << to_string(table.error());

    EXPECT_EQ(table->frame_count(), 4u);
    EXPECT_EQ(table->view_at(0).size(), 1u);
    EXPECT_TRUE(table->view_at(1).empty()) << "frame 1 is a hole in the table";
    EXPECT_TRUE(table->view_at(2).empty()) << "frame 2 is a hole in the table";
    EXPECT_EQ(table->view_at(3).size(), 1u);
    EXPECT_TRUE(table->view_at(4).empty());
    EXPECT_TRUE(table->view_at(9999).empty());
}

TEST(TruthTableLoad, ReadsAnEmptyFileAsAnEmptyTable) {
    test::TempFile file{".csv"};
    write_text(file, "");

    const auto table = TruthTable::load(file.path());
    ASSERT_TRUE(table.has_value()) << to_string(table.error());
    EXPECT_TRUE(table->view_all().empty());
    EXPECT_EQ(table->frame_count(), 0u);
    EXPECT_TRUE(table->view_at(0).empty());
}

/// Nothing promises a truth file arrives sorted -- it could be concatenated
/// from two runs -- and the index needs each frame contiguous.
TEST(TruthTableLoad, SortsRecordsThatArriveOutOfOrder) {
    test::TempFile file{".csv"};
    write_text(file, "2, 1, 6, 6, 600\n"
                     "0, 1, 2, 2, 200\n"
                     "2, 0, 5, 5, 500\n"
                     "0, 0, 1, 1, 100\n");

    const auto table = TruthTable::load(file.path());
    ASSERT_TRUE(table.has_value()) << to_string(table.error());

    ASSERT_EQ(table->view_at(0).size(), 2u);
    ASSERT_EQ(table->view_at(2).size(), 2u);
    EXPECT_EQ(table->view_at(0)[0].target_id, 0u);
    EXPECT_EQ(table->view_at(2)[0].target_id, 0u);
    EXPECT_EQ(table->view_at(2)[0].row, 5.0);
}

/// Blank lines and padding are hand-editing artefacts, not records. Neither
/// should become a row, and neither should fail the load.
TEST(TruthTableLoad, SkipsBlankLinesAndToleratesSurroundingSpace) {
    test::TempFile file{".csv"};
    write_text(file, "\n"
                     "  0,0,1,1,100  \n"
                     "\n"
                     "0 , 1 , 2 , 2 , 200\n"
                     "\n");

    const auto table = TruthTable::load(file.path());
    ASSERT_TRUE(table.has_value()) << to_string(table.error());
    ASSERT_EQ(table->view_at(0).size(), 2u);
    expect_same(table->view_at(0)[0], record(0, 0, 1.0, 1.0, 100.0));
    expect_same(table->view_at(0)[1], record(0, 1, 2.0, 2.0, 200.0));
}

/// A line that is not five numbers is refused rather than half-read. Silently
/// keeping the fields that did parse would score a run against truth that is
/// partly invented.
TEST(TruthTableLoad, ReportsMalformedRecordRatherThanReadingPartOfALine) {
    for (const std::string_view bad : {
             "0, 0, 1, 1\n",             // a field short
             "0, 0, 1, 1, 100, 7\n",     // a field long
             "0, 0, 1, 1, abc\n",        // not a number
             "0 0, 1, 1, 100\n",         // a missing separator
             "frame, target, r, c, a\n", // a header line
             "0, 0, 1, 1, 100x\n",       // trailing garbage on a good field
             "-1, 0, 1, 1, 100\n",       // a negative frame id
         }) {
        test::TempFile file{".csv"};
        write_text(file, bad);

        const auto table = TruthTable::load(file.path());
        ASSERT_FALSE(table.has_value()) << "accepted: " << bad;
        EXPECT_EQ(table.error(), ParseError::MalformedRecord) << bad;
    }
}

/// The index is one slot per frame, so the frame id sizes an allocation. A
/// corrupt id has to be refused before it is believed.
TEST(TruthTableLoad, RefusesAFrameIdBeyondTheCeiling) {
    test::TempFile file{".csv"};
    write_text(file, "0, 0, 1, 1, 100\n"
                     "4000000000, 0, 2, 2, 200\n");

    const auto table = TruthTable::load(file.path());
    ASSERT_FALSE(table.has_value());
    EXPECT_EQ(table.error(), ParseError::MalformedRecord);
}

} // namespace
} // namespace opir
