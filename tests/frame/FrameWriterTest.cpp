#include "frame/FrameWriter.hpp"
#include "core/Error.hpp"
#include "core/Types.hpp"
#include "frame/Frame.hpp"

#include "support/TempFile.hpp"

#include <cstdint>
#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstring>
#include <numeric>
#include <vector>

namespace opir {
namespace {

/// Small enough to write out an expected buffer by hand, and deliberately
/// non-square so a rows/columns mix-up cannot pass unnoticed.
constexpr std::size_t kRows = 4;
constexpr std::size_t kColumns = 3;
constexpr std::size_t kPixelsPerFrame = kRows * kColumns;
constexpr std::size_t kHeaderBytes = sizeof(FrameHeader);
constexpr std::size_t kFrameBytes =
    kHeaderBytes + kPixelsPerFrame * sizeof(Pixel);

/// 1, 2, 3, ... so that every pixel is distinguishable from every other and
/// none of them is the zero a buffer would hold if it were never written.
std::vector<Pixel> ramp(std::size_t count, Pixel first = 1) {
    std::vector<Pixel> values(count);
    std::iota(values.begin(), values.end(), first);
    return values;
}

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

// ---------------------------------------------------------------------------
// Construction: the file is opened once, up front.
// ---------------------------------------------------------------------------

TEST(FrameWriterConstruction, ThrowsWhenThePathCannotBeOpened) {
    EXPECT_THROW(FrameWriter(test::unopenable_path(), kRows, kColumns), Error);
}

/// The shape goes into every header as a uint32, so it is checked once here
/// rather than being allowed to overflow or to describe an empty detector.
TEST(FrameWriterConstruction, RejectsAnOutOfRangeFrameShape) {
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
    EXPECT_TRUE(file.exists())
        << "opening is eager, so an unwritable destination is reported before "
           "any simulation time is spent";
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

// ---------------------------------------------------------------------------
// write_frame: what a frame is made of.
// ---------------------------------------------------------------------------

TEST(FrameWriterWriteFrame, WritesAHeaderThenRowsTimesColumnsPixelsInOrder) {
    test::TempFile file{".bin"};
    {
        FrameWriter writer{file.path(), kRows, kColumns};
        writer.write_frame(0, 0.0, ramp(kPixelsPerFrame));
    }

    EXPECT_EQ(file.size(), kFrameBytes)
        << "a frame is a " << kHeaderBytes << "-byte header plus rows * "
        << "columns * sizeof(Pixel) bytes";
    EXPECT_EQ(payload_of(file, 0), ramp(kPixelsPerFrame));
}

TEST(FrameWriterWriteFrame, StampsTheHeaderWithMagicVersionShapeIdAndTime) {
    test::TempFile file{".bin"};
    {
        FrameWriter writer{file.path(), kRows, kColumns};
        writer.write_frame(7, 2.5, ramp(kPixelsPerFrame));
    }

    const FrameHeader header = header_of(file, 0);
    EXPECT_EQ(header.magic, kFrameMagic);
    EXPECT_EQ(header.version, kFrameVersion);
    EXPECT_EQ(header.rows, kRows);
    EXPECT_EQ(header.cols, kColumns);
    EXPECT_EQ(header.frame_id, 7u);
    EXPECT_EQ(header.timestamp_us, 2'500'000u)
        << "seconds in, microseconds out";
    EXPECT_EQ(header.reserved, 0u) << "reserved bytes must not carry garbage";
    EXPECT_EQ(header.reserved2, 0u);
}

/// The frames are back to back with nothing between them, so a reader finds
/// frame N by stepping over N whole frames and checking the magic it lands on.
TEST(FrameWriterWriteFrame, AppendsFramesBackToBackWithNothingBetweenThem) {
    constexpr std::size_t kFrames = 3;
    test::TempFile file{".bin"};
    {
        FrameWriter writer{file.path(), kRows, kColumns};
        for (std::size_t f = 0; f < kFrames; ++f)
            writer.write_frame(
                static_cast<FrameId>(f), 0.5 * static_cast<double>(f),
                ramp(kPixelsPerFrame, static_cast<Pixel>(100 * (f + 1))));
    }

    ASSERT_EQ(file.size(), kFrames * kFrameBytes);
    for (std::size_t f = 0; f < kFrames; ++f) {
        EXPECT_EQ(header_of(file, f).magic, kFrameMagic) << "frame " << f;
        EXPECT_EQ(header_of(file, f).frame_id, f);
        EXPECT_EQ(payload_of(file, f),
                  ramp(kPixelsPerFrame, static_cast<Pixel>(100 * (f + 1))))
            << "frame " << f << " is not where it was written";
    }
}

/// The pixels go out as raw memory, which makes the byte order of the file the
/// byte order of whatever machine produced it. This pins the layout we actually
/// get here; it is also the thing to revisit if the format ever has to be read
/// on a big-endian machine.
TEST(FrameWriterWriteFrame, StoresEachPixelInHostByteOrder) {
    static_assert(std::endian::native == std::endian::little,
                  "this test spells out the little-endian layout");

    test::TempFile file{".bin"};
    {
        FrameWriter writer{file.path(), 1, 2};
        writer.write_frame(0, 0.0, std::vector<Pixel>{0x0102, 0xFF00});
    }

    const std::vector<std::byte> raw = file.bytes();
    ASSERT_EQ(raw.size(), kHeaderBytes + 4);
    EXPECT_EQ(raw[kHeaderBytes + 0], std::byte{0x02}) << "low byte first";
    EXPECT_EQ(raw[kHeaderBytes + 1], std::byte{0x01});
    EXPECT_EQ(raw[kHeaderBytes + 2], std::byte{0x00});
    EXPECT_EQ(raw[kHeaderBytes + 3], std::byte{0xFF});
}

// ---------------------------------------------------------------------------
// write_frame: buffers that are not exactly one frame.
// ---------------------------------------------------------------------------

/// A rejected frame must leave nothing behind, not even its header, or the
/// stream would desync; and the writer stays usable so a caller can log and
/// carry on rather than tear the whole run down.
TEST(FrameWriterWriteFrame, RejectsATooSmallBufferWithoutWritingAnything) {
    test::TempFile file{".bin"};
    {
        FrameWriter writer{file.path(), kRows, kColumns};
        EXPECT_THROW(writer.write_frame(0, 0.0, {}), Error);
        EXPECT_THROW(writer.write_frame(0, 0.0, ramp(kPixelsPerFrame - 1)),
                     Error);
        EXPECT_EQ(file.size(), 0u);

        EXPECT_NO_THROW(writer.write_frame(0, 0.0, ramp(kPixelsPerFrame)));
    }

    EXPECT_EQ(file.size(), kFrameBytes);
    EXPECT_EQ(payload_of(file, 0), ramp(kPixelsPerFrame));
}

/// An oversized buffer is accepted and silently trimmed, which is what lets a
/// caller reuse one large scratch buffer across differently sized writers.
TEST(FrameWriterWriteFrame, IgnoresPixelsPastTheEndOfTheFrame) {
    test::TempFile file{".bin"};
    {
        FrameWriter writer{file.path(), kRows, kColumns};
        writer.write_frame(0, 0.0, ramp(kPixelsPerFrame + 5));
    }

    EXPECT_EQ(file.size(), kFrameBytes);
    EXPECT_EQ(payload_of(file, 0), ramp(kPixelsPerFrame))
        << "the frame is taken from the front of the buffer";
}

} // namespace
} // namespace opir
