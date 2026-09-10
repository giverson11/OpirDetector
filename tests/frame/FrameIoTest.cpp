#include "core/ParseError.hpp"
#include "core/Types.hpp"
#include "frame/Frame.hpp"
#include "frame/FrameReader.hpp"
#include "frame/FrameWriter.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <numeric>
#include <string_view>
#include <vector>

namespace opir {
namespace {

constexpr std::size_t kRows = 4;
constexpr std::size_t kColumns = 3;
constexpr std::size_t kPixelsPerFrame = kRows * kColumns;
constexpr std::uintmax_t kPayloadBytes = kPixelsPerFrame * sizeof(Pixel);

/// A path under the temp directory, cleared on the way in so a previous run's
/// leftovers can never be mistaken for this one's output.
std::filesystem::path scratch(std::string_view name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    return path;
}

/// first, first + 1, ... so every pixel is distinguishable from every other,
/// and none of them is the zero an unwritten buffer would hold.
std::vector<Pixel> ramp(std::size_t count, Pixel first = 1) {
    std::vector<Pixel> values(count);
    std::iota(values.begin(), values.end(), first);
    return values;
}

/// Writes `count` frames through the real writer: ids 0.., timestamps at
/// half-second steps, frame f carrying the ramp starting at 100 * (f + 1).
void write_frames(const std::filesystem::path &path, std::size_t count) {
    FrameWriter writer{path, kRows, kColumns};
    for (std::size_t f = 0; f < count; ++f)
        writer.write_frame(
            static_cast<FrameId>(f), 0.5 * static_cast<double>(f),
            ramp(kPixelsPerFrame, static_cast<Pixel>(100 * (f + 1))));
}

/// The pairing that matters: every field the writer put down comes back, and
/// frames stay in order behind each other rather than blurring together.
TEST(FrameIo, RoundTripsWhatTheWriterWrote) {
    const auto path = scratch("opir_frame_roundtrip.bin");
    write_frames(path, 3);

    FrameReader reader{path};
    for (std::size_t f = 0; f < 3; ++f) {
        const auto frame = reader.next();
        ASSERT_TRUE(frame.has_value()) << "frame " << f;

        EXPECT_EQ(frame->id, f);
        EXPECT_DOUBLE_EQ(frame->t, 0.5 * static_cast<double>(f));
        ASSERT_EQ(frame->px.extent(0), kRows);
        ASSERT_EQ(frame->px.extent(1), kColumns);

        const std::vector<Pixel> expected =
            ramp(kPixelsPerFrame, static_cast<Pixel>(100 * (f + 1)));
        for (std::size_t r = 0; r < kRows; ++r)
            for (std::size_t c = 0; c < kColumns; ++c)
                EXPECT_EQ((frame->px[r, c]), expected[r * kColumns + c])
                    << "frame " << f << " at " << r << ", " << c;
    }
    std::filesystem::remove(path);
}

/// A file that stops on a frame boundary has ended, not failed, and keeps
/// saying so rather than reporting something new on a second call. An empty
/// file is the zero-frame case of the same thing -- and the one that turns a
/// failed scene_gen into a detector run that silently reports nothing.
TEST(FrameIo, ReportsEndOfStreamOnAnEmptyFileAndAfterTheLastFrame) {
    const auto empty = scratch("opir_frame_empty.bin");
    write_frames(empty, 0);
    {
        FrameReader reader{empty};
        const auto frame = reader.next();
        ASSERT_FALSE(frame.has_value());
        EXPECT_EQ(frame.error(), ParseError::EndOfStream);
    }
    std::filesystem::remove(empty);

    const auto path = scratch("opir_frame_end.bin");
    write_frames(path, 2);

    FrameReader reader{path};
    EXPECT_TRUE(reader.next().has_value());
    EXPECT_TRUE(reader.next().has_value());
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto frame = reader.next();
        ASSERT_FALSE(frame.has_value());
        EXPECT_EQ(frame.error(), ParseError::EndOfStream) << "call " << attempt;
    }
    std::filesystem::remove(path);
}

/// The distinction the whole read path exists to make: a file cut off part way
/// through, whether inside the pixels or inside the header, is corrupt and
/// must not look like a clean end.
TEST(FrameIo, ReportsShortReadOnATruncatedFrameOrHeader) {
    for (const std::uintmax_t missing :
         {std::uintmax_t{1}, std::uintmax_t{5}, kPayloadBytes,
          kPayloadBytes + sizeof(FrameHeader) - 4}) {
        const auto path = scratch("opir_frame_short.bin");
        write_frames(path, 2);
        std::filesystem::resize_file(path, std::filesystem::file_size(path) -
                                               missing);

        FrameReader reader{path};
        ASSERT_TRUE(reader.next().has_value()) << "missing " << missing;
        const auto frame = reader.next();
        ASSERT_FALSE(frame.has_value()) << "missing " << missing;
        EXPECT_EQ(frame.error(), ParseError::ShortRead)
            << "missing " << missing;
        std::filesystem::remove(path);
    }
}

} // namespace
} // namespace opir
