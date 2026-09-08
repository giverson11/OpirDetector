#include "frame/FrameWriter.hpp"
#include "core/Error.hpp"
#include "core/Types.hpp"
#include "frame/Frame.hpp"

#include "support/Ramp.hpp"
#include "support/TempFile.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <vector>

namespace opir {
namespace {

using test::ramp;

/// Small enough to write out an expected buffer by hand, and deliberately
/// non-square so a rows/columns mix-up cannot pass unnoticed.
constexpr std::size_t kRows = 4;
constexpr std::size_t kColumns = 3;
constexpr std::size_t kPixelsPerFrame = kRows * kColumns;
constexpr std::size_t kHeaderBytes = sizeof(FrameHeader);
constexpr std::size_t kFrameBytes =
    kHeaderBytes + kPixelsPerFrame * sizeof(Pixel);

/// The header of frame `index`, or a default-constructed one if the file is too
/// short to hold it (which the caller's size assertion will already have said).
FrameHeader header_of(const test::TempFile &file, std::size_t index) {
    const std::vector<std::byte> raw = file.bytes();
    FrameHeader header{};
    const std::size_t offset = index * kFrameBytes;
    if (raw.size() >= offset + kHeaderBytes)
        std::memcpy(&header, raw.data() + offset, kHeaderBytes);
    return header;
}

/// The pixels of frame `index`, with the header skipped.
std::vector<Pixel> payload_of(const test::TempFile &file, std::size_t index) {
    const std::vector<std::byte> raw = file.bytes();
    const std::size_t offset = index * kFrameBytes + kHeaderBytes;
    const std::size_t bytes = kPixelsPerFrame * sizeof(Pixel);
    if (raw.size() < offset + bytes)
        return {};
    std::vector<Pixel> out(kPixelsPerFrame);
    std::memcpy(out.data(), raw.data() + offset, bytes);
    return out;
}

/// The file is opened and the shape checked up front, so a bad destination or
/// a shape that cannot go into a uint32 header is reported before any
/// simulation time is spent.
TEST(FrameWriterConstruction, ThrowsForAnUnopenablePathOrAnOutOfRangeShape) {
    EXPECT_THROW(FrameWriter(test::unopenable_path(), kRows, kColumns), Error);

    test::TempFile file{".bin"};
    EXPECT_THROW(FrameWriter(file.path(), 0, kColumns), Error);
    EXPECT_THROW(FrameWriter(file.path(), kRows, 0), Error);
    EXPECT_THROW(FrameWriter(file.path(), kMaxFrameDim + 1, kColumns), Error);
    EXPECT_NO_THROW(FrameWriter(file.path(), kMaxFrameDim, kMaxFrameDim));
}

TEST(FrameWriterConstruction, CreatesTheFileEmptyAndTruncatesAnExistingOne) {
    test::TempFile file{".bin"};
    {
        FrameWriter writer{file.path(), kRows, kColumns};
    }
    EXPECT_TRUE(file.exists());
    EXPECT_EQ(file.size(), 0u);

    {
        FrameWriter writer{file.path(), kRows, kColumns};
        writer.write_frame(0, 0.0, ramp(kPixelsPerFrame));
    }
    ASSERT_EQ(file.size(), kFrameBytes);

    {
        FrameWriter writer{file.path(), kRows, kColumns};
    }
    EXPECT_EQ(file.size(), 0u)
        << "a second run must not append to the first run's output";
}

/// A frame is a header stamped with magic, version, shape, id and timestamp,
/// followed by rows * columns pixels in row-major order. Frames sit back to
/// back with nothing between them, so a reader finds frame N by stepping over
/// N whole frames and checking the magic it lands on.
TEST(FrameWriterWriteFrame, WritesFramesBackToBackEachAHeaderThenPixels) {
    constexpr std::size_t kFrames = 3;
    test::TempFile file{".bin"};
    {
        FrameWriter writer{file.path(), kRows, kColumns};
        for (std::size_t f = 0; f < kFrames; ++f)
            writer.write_frame(
                static_cast<FrameId>(f), 0.5 * static_cast<double>(f),
                ramp(kPixelsPerFrame, static_cast<Pixel>(100 * (f + 1))));
    }

    ASSERT_EQ(file.size(), kFrames * kFrameBytes)
        << "a frame is a " << kHeaderBytes << "-byte header plus rows * "
        << "columns * sizeof(Pixel) bytes";
    for (std::size_t f = 0; f < kFrames; ++f) {
        const FrameHeader header = header_of(file, f);
        EXPECT_EQ(header.magic, kFrameMagic) << "frame " << f;
        EXPECT_EQ(header.version, kFrameVersion);
        EXPECT_EQ(header.rows, kRows);
        EXPECT_EQ(header.cols, kColumns);
        EXPECT_EQ(header.frame_id, f);
        EXPECT_EQ(header.timestamp_us, 500'000 * f)
            << "seconds in, microseconds out";
        EXPECT_EQ(header.reserved, 0u)
            << "reserved bytes must not carry garbage";
        EXPECT_EQ(header.reserved2, 0u);
        EXPECT_EQ(payload_of(file, f),
                  ramp(kPixelsPerFrame, static_cast<Pixel>(100 * (f + 1))))
            << "frame " << f << " is not where it was written";
    }
}

/// A rejected frame must leave nothing behind, not even its header, or the
/// stream would desync; and the writer stays usable afterwards. An oversized
/// buffer is accepted and trimmed from the front, which is what lets a caller
/// reuse one large scratch buffer across differently sized writers.
TEST(FrameWriterWriteFrame, RejectsATooSmallBufferAndTrimsAnOversizedOne) {
    test::TempFile file{".bin"};
    {
        FrameWriter writer{file.path(), kRows, kColumns};
        EXPECT_THROW(writer.write_frame(0, 0.0, {}), Error);
        EXPECT_THROW(writer.write_frame(0, 0.0, ramp(kPixelsPerFrame - 1)),
                     Error);
        EXPECT_EQ(file.size(), 0u);

        EXPECT_NO_THROW(writer.write_frame(0, 0.0, ramp(kPixelsPerFrame + 5)));
    }

    EXPECT_EQ(file.size(), kFrameBytes);
    EXPECT_EQ(payload_of(file, 0), ramp(kPixelsPerFrame))
        << "the frame is taken from the front of the buffer";
}

} // namespace
} // namespace opir
