#include "sim/TruthWriter.hpp"
#include "core/Error.hpp"
#include "core/Types.hpp"
#include "sim/SceneSimulator.hpp"

#include "support/TempFile.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
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

/// The file split on newlines, with the trailing empty piece after the final
/// newline dropped, so a test can talk about "lines" without counting
/// terminators.
std::vector<std::string> lines_of(const test::TempFile &file) {
    const std::string text = file.text();
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

// ---------------------------------------------------------------------------
// Construction.
// ---------------------------------------------------------------------------

TEST(TruthWriterConstruction, ThrowsWhenThePathCannotBeOpened) {
    EXPECT_THROW(TruthWriter(test::unopenable_path()), Error);
}

TEST(TruthWriterConstruction, CreatesTheFileEmptyAndTruncatesAnExistingOne) {
    test::TempFile file{".csv"};
    {
        TruthWriter writer{file.path()};
    }
    EXPECT_TRUE(file.exists());
    EXPECT_EQ(file.size(), 0u);

    {
        TruthWriter writer{file.path()};
        writer.write_truth(std::vector{record(0, 0, 1.0, 2.0, 3.0)});
    }
    ASSERT_GT(file.size(), 0u);

    {
        TruthWriter writer{file.path()};
    }
    EXPECT_EQ(file.size(), 0u)
        << "a second run must not append to the first run's output";
}

// ---------------------------------------------------------------------------
// write_truth: the shape of a row.
// ---------------------------------------------------------------------------

/// The field order is frame, target, row, column, amplitude, comma-and-space
/// separated, with a terminator on every line and no header ahead of them.
/// Anything reading Truth.csv depends on exactly this, and a consumer that
/// skipped a header line would silently drop frame 0.
TEST(TruthWriterWriteTruth, WritesFieldsInFrameTargetRowColumnAmplitudeOrder) {
    test::TempFile file{".csv"};
    {
        TruthWriter writer{file.path()};
        writer.write_truth(std::vector{record(7, 2, 10.5, 20.25, 1500.0)});
    }

    EXPECT_EQ(file.text(), "7, 2, 10.5, 20.25, 1500\n");
}

/// std::format writes the shortest representation that round-trips, so a whole
/// number loses its decimal point entirely and a very large or small one turns
/// into exponent notation. Both are valid doubles to a parser that uses strtod,
/// and both surprise anything doing its own naive column parsing.
TEST(TruthWriterWriteTruth, UsesShortestRoundTripFormattingForDoubles) {
    test::TempFile file{".csv"};
    {
        TruthWriter writer{file.path()};
        writer.write_truth(std::vector{record(0, 0, 24.0, 24.0, 2000.0),
                                       record(1, 0, 0.1, -3.5, 1e20)});
    }

    const std::vector<std::string> lines = lines_of(file);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "0, 0, 24, 24, 2000") << "24.0 is written as \"24\"";
    EXPECT_EQ(lines[1], "1, 0, 0.1, -3.5, 1e+20");
}

// ---------------------------------------------------------------------------
// write_truth: how many rows, and in what order.
// ---------------------------------------------------------------------------

/// Target ids are the simulator's insertion indices and stay stable when an
/// earlier target leaves the frame, so a span's ids need not be contiguous and
/// the writer must not renumber them.
TEST(TruthWriterWriteTruth, WritesOneLinePerRecordInSpanOrder) {
    test::TempFile file{".csv"};
    {
        TruthWriter writer{file.path()};
        writer.write_truth(std::vector{record(3, 0, 1.0, 1.0, 100.0),
                                       record(3, 2, 2.0, 2.0, 200.0),
                                       record(3, 5, 3.0, 3.0, 300.0)});
    }

    EXPECT_EQ(lines_of(file),
              (std::vector<std::string>{"3, 0, 1, 1, 100", "3, 2, 2, 2, 200",
                                        "3, 5, 3, 3, 300"}));
}

/// One call per frame is how the simulator drives this, so the calls have to
/// accumulate rather than replace.
TEST(TruthWriterWriteTruth, AppendsAcrossCalls) {
    test::TempFile file{".csv"};
    {
        TruthWriter writer{file.path()};
        writer.write_truth(std::vector{record(0, 0, 1.0, 1.0, 100.0)});
        writer.write_truth(std::vector{record(1, 0, 2.0, 2.0, 100.0)});
        writer.write_truth(std::vector{record(2, 0, 3.0, 3.0, 100.0)});
    }

    EXPECT_EQ(lines_of(file),
              (std::vector<std::string>{"0, 0, 1, 1, 100", "1, 0, 2, 2, 100",
                                        "2, 0, 3, 3, 100"}));
}

/// A frame in which every target has left the scene is a normal outcome, not an
/// error, and it must not leave a blank line in the middle of the file.
TEST(TruthWriterWriteTruth, WritesNothingForAnEmptySpan) {
    test::TempFile file{".csv"};
    {
        TruthWriter writer{file.path()};
        writer.write_truth(std::vector{record(0, 0, 1.0, 1.0, 100.0)});
        writer.write_truth({});
        writer.write_truth(std::vector{record(2, 0, 3.0, 3.0, 100.0)});
    }

    EXPECT_EQ(lines_of(file), (std::vector<std::string>{"0, 0, 1, 1, 100",
                                                        "2, 0, 3, 3, 100"}));
}

/// Deciding which targets belong in the truth table is the simulator's job.
/// The writer records whatever it is handed, off-frame coordinates included.
TEST(TruthWriterWriteTruth, RecordsOffFrameAndNegativeCoordinatesUnchanged) {
    test::TempFile file{".csv"};
    {
        TruthWriter writer{file.path()};
        writer.write_truth(std::vector{record(0, 0, -1.5, 999.5, 0.0)});
    }

    EXPECT_EQ(file.text(), "0, 0, -1.5, 999.5, 0\n");
}

} // namespace
} // namespace opir
