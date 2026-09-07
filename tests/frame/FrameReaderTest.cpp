#include "frame/FrameReader.hpp"
#include "core/Error.hpp"
#include "core/ParseError.hpp"
#include "core/Types.hpp"
#include "frame/Frame.hpp"
#include "frame/FrameWriter.hpp"

#include "support/TempFile.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <ios>
#include <numeric>
#include <span>
#include <vector>

namespace opir {
namespace {

constexpr std::size_t kRows = 4;
constexpr std::size_t kColumns = 3;
constexpr std::size_t kPixelsPerFrame = kRows * kColumns;

std::vector<Pixel> ramp(std::size_t count, Pixel first = 1) {
    std::vector<Pixel> values(count);
    std::iota(values.begin(), values.end(), first);
    return values;
}

/// Writes `count` frames through the real writer, ids 0.. and timestamps at
/// half-second steps, frame f carrying the ramp starting at 100 * (f + 1).
void write_frames(const test::TempFile &file, std::size_t count) {
    FrameWriter writer{file.path(), kRows, kColumns};
    for (std::size_t f = 0; f < count; ++f)
        writer.write_frame(
            static_cast<FrameId>(f), 0.5 * static_cast<double>(f),
            ramp(kPixelsPerFrame, static_cast<Pixel>(100 * (f + 1))));
}

/// Writes one frame from a header the test has hand-built, so that a header the
/// writer would never produce can still be put in front of a reader.
void write_corrupt_frame(const test::TempFile &file, const FrameHeader &header,
                         std::size_t pixel_count = kPixelsPerFrame) {
    std::ofstream out{file.path(), std::ios::binary | std::ios::app};
    const auto header_bytes = std::as_bytes(std::span{&header, 1});
    out.write(reinterpret_cast<const char *>(header_bytes.data()),
              static_cast<std::streamsize>(header_bytes.size()));
    const std::vector<Pixel> pixels = ramp(pixel_count);
    const auto pixel_bytes = std::as_bytes(std::span{pixels});
    out.write(reinterpret_cast<const char *>(pixel_bytes.data()),
              static_cast<std::streamsize>(pixel_bytes.size()));
}

/// A header the writer itself would emit, as the starting point for a mutation.
FrameHeader good_header(FrameId id = 0) {
    return FrameHeader{.rows = static_cast<std::uint32_t>(kRows),
                       .cols = static_cast<std::uint32_t>(kColumns),
                       .frame_id = id,
                       .timestamp_us = 0};
}

/// Drops `n` bytes from the end of a file, to stand in for a capture that was
/// interrupted or a transfer that did not finish.
void truncate_by(const test::TempFile &file, std::uintmax_t n) {
    std::filesystem::resize_file(file.path(), file.size() - n);
}

// ---------------------------------------------------------------------------

TEST(FrameReaderConstruction, ThrowsWhenThePathCannotBeOpened) {
    EXPECT_THROW(FrameReader{test::unopenable_path()}, Error);
}

/// The whole point of the header: the reader is told a path and nothing else,
/// and recovers the shape, the ids and the timestamps from the stream.
TEST(FrameReaderNext, RoundTripsWhatTheWriterWrote) {
    test::TempFile file{".bin"};
    write_frames(file, 3);

    FrameReader reader{file.path()};
    for (std::size_t f = 0; f < 3; ++f) {
        const auto frame = reader.next();
        ASSERT_TRUE(frame.has_value())
            << "frame " << f << ": " << to_string(frame.error());
        EXPECT_EQ(frame->id, f);
        EXPECT_NEAR(frame->t, 0.5 * static_cast<double>(f), 1e-9);
        ASSERT_EQ(frame->px.extent(0), kRows);
        ASSERT_EQ(frame->px.extent(1), kColumns);

        const std::vector<Pixel> expected =
            ramp(kPixelsPerFrame, static_cast<Pixel>(100 * (f + 1)));
        for (std::size_t r = 0; r < kRows; ++r)
            for (std::size_t c = 0; c < kColumns; ++c)
                EXPECT_EQ((frame->px[r, c]), expected[r * kColumns + c])
                    << "frame " << f << " at " << r << ", " << c;
    }
}

/// A file that stops on a frame boundary has ended, not failed, and keeps
/// saying so rather than reporting something new on a second call.
TEST(FrameReaderNext, ReportsEndOfStreamAfterTheLastFrameAndStaysThere) {
    test::TempFile file{".bin"};
    write_frames(file, 2);

    FrameReader reader{file.path()};
    EXPECT_TRUE(reader.next().has_value());
    EXPECT_TRUE(reader.next().has_value());

    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto frame = reader.next();
        ASSERT_FALSE(frame.has_value());
        EXPECT_EQ(frame.error(), ParseError::EndOfStream) << "call " << attempt;
    }
}

TEST(FrameReaderNext, ReportsEndOfStreamOnAnEmptyFile) {
    test::TempFile file{".bin"};
    write_frames(file, 0);

    FrameReader reader{file.path()};
    const auto frame = reader.next();
    ASSERT_FALSE(frame.has_value());
    EXPECT_EQ(frame.error(), ParseError::EndOfStream);
}

/// The distinction the whole read path exists to make: a file cut off part way
/// through is corrupt, and must not look like a clean end.
TEST(FrameReaderNext, ReportsShortReadOnATruncatedFrame) {
    for (const std::uintmax_t missing : {std::uintmax_t{1}, std::uintmax_t{5},
                                         kPixelsPerFrame * sizeof(Pixel)}) {
        test::TempFile file{".bin"};
        write_frames(file, 2);
        truncate_by(file, missing);

        FrameReader reader{file.path()};
        EXPECT_TRUE(reader.next().has_value()) << "missing " << missing;

        const auto frame = reader.next();
        ASSERT_FALSE(frame.has_value()) << "missing " << missing;
        EXPECT_EQ(frame.error(), ParseError::ShortRead)
            << "a frame missing " << missing << " bytes is truncated, not a "
            << "clean end";
    }
}

TEST(FrameReaderNext, ReportsShortReadOnATruncatedHeader) {
    test::TempFile file{".bin"};
    write_frames(file, 1);
    // One frame, plus a few bytes of a header that never arrived.
    {
        std::ofstream out{file.path(), std::ios::binary | std::ios::app};
        const char partial[4]{'O', 'P', 'I', 'R'};
        out.write(partial, sizeof(partial));
    }

    FrameReader reader{file.path()};
    EXPECT_TRUE(reader.next().has_value());

    const auto frame = reader.next();
    ASSERT_FALSE(frame.has_value());
    EXPECT_EQ(frame.error(), ParseError::ShortRead);
}

/// Everything the header exists to catch, one mutation at a time.
TEST(FrameReaderNext, RejectsAHeaderItDoesNotRecognise) {
    struct Case {
        const char *name;
        FrameHeader header;
        ParseError expected;
    };
    std::vector<Case> cases;

    FrameHeader bad_magic = good_header();
    bad_magic.magic = {'N', 'O', 'P', 'E'};
    cases.push_back({"bad magic", bad_magic, ParseError::BadMagic});

    FrameHeader bad_version = good_header();
    bad_version.version = kFrameVersion + 1;
    cases.push_back(
        {"future version", bad_version, ParseError::UnsupportedVersion});

    FrameHeader zero_rows = good_header();
    zero_rows.rows = 0;
    cases.push_back({"zero rows", zero_rows, ParseError::BadDimensions});

    FrameHeader huge = good_header();
    huge.cols = kMaxFrameDim + 1;
    cases.push_back({"oversized columns", huge, ParseError::BadDimensions});

    for (const Case &c : cases) {
        test::TempFile file{".bin"};
        write_corrupt_frame(file, c.header);

        FrameReader reader{file.path()};
        const auto frame = reader.next();
        ASSERT_FALSE(frame.has_value()) << c.name;
        EXPECT_EQ(frame.error(), c.expected) << c.name;
    }
}

/// Every frame in a file comes off one detector, so a shape that changes part
/// way through means two captures were concatenated, not that the frame is
/// merely unusual.
TEST(FrameReaderNext, RejectsAShapeThatChangesMidStream) {
    test::TempFile file{".bin"};
    write_frames(file, 1);

    FrameHeader different = good_header(1);
    different.rows = kRows + 1;
    write_corrupt_frame(file, different, (kRows + 1) * kColumns);

    FrameReader reader{file.path()};
    EXPECT_TRUE(reader.next().has_value());

    const auto frame = reader.next();
    ASSERT_FALSE(frame.has_value());
    EXPECT_EQ(frame.error(), ParseError::BadDimensions);
}

} // namespace
} // namespace opir
