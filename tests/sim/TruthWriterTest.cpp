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

TEST(TruthWriterConstruction, ThrowsWhenThePathCannotBeOpened) {
    EXPECT_THROW(TruthWriter{test::unopenable_path()}, Error);
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

/// The field order is frame, target, row, column, amplitude, comma-and-space
/// separated, with a terminator on every line and no header ahead of them.
/// Anything reading Truth.csv depends on exactly this, and a consumer that
/// skipped a header line would silently drop frame 0.
///
/// Doubles use std::format's shortest round-trip form: a whole number loses
/// its decimal point and a very large one turns into exponent notation. Both
/// are valid to strtod and both surprise naive column parsing. Off-frame and
/// negative coordinates are written as handed over; deciding what belongs in
/// the table is the simulator's job.
TEST(TruthWriterWriteTruth, WritesOneLinePerRecordAsFrameTargetRowColAmp) {
    test::TempFile file{".csv"};
    {
        TruthWriter writer{file.path()};
        writer.write_truth(std::vector{
            record(7, 2, 10.5, 20.25, 1500.0), record(0, 0, 24.0, 24.0, 2000.0),
            record(1, 0, 0.1, -3.5, 1e20), record(0, 0, -1.5, 999.5, 0.0)});
    }

    EXPECT_EQ(file.text(), "7, 2, 10.5, 20.25, 1500\n"
                           "0, 0, 24, 24, 2000\n"
                           "1, 0, 0.1, -3.5, 1e+20\n"
                           "0, 0, -1.5, 999.5, 0\n");
}

/// One call per frame is how the simulator drives this, so calls accumulate
/// in order; a frame in which every target has left is a normal outcome and
/// must not leave a blank line. Ids need not be contiguous and are never
/// renumbered.
TEST(TruthWriterWriteTruth, AccumulatesAcrossCallsAndSkipsAnEmptySpan) {
    test::TempFile file{".csv"};
    {
        TruthWriter writer{file.path()};
        writer.write_truth(std::vector{record(3, 0, 1.0, 1.0, 100.0),
                                       record(3, 5, 2.0, 2.0, 200.0)});
        writer.write_truth({});
        writer.write_truth(std::vector{record(5, 0, 3.0, 3.0, 300.0)});
    }

    EXPECT_EQ(lines_of(file),
              (std::vector<std::string>{"3, 0, 1, 1, 100", "3, 5, 2, 2, 200",
                                        "5, 0, 3, 3, 300"}));
}

} // namespace
} // namespace opir
