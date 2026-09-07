#include "frame/FrameWriter.hpp"
#include "core/Error.hpp"
#include "core/Io.hpp"
#include "core/Types.hpp"
#include "frame/Frame.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ios>
#include <span>

namespace opir {

FrameWriter::FrameWriter(std::filesystem::path path, size_t rows, size_t cols)
    : out_{open_for_write(path, std::ios::binary)}, rows_{rows}, cols_{cols} {
    if (rows == 0 || cols == 0 || rows > kMaxFrameDim || cols > kMaxFrameDim) {
        throw Error(std::format("frame shape {} x {} is out of range 1..{}",
                                rows, cols, kMaxFrameDim));
    }
}

void FrameWriter::write_frame(FrameId id, double timestamp_s,
                              std::span<const Pixel> pixels) {
    if (pixels.size() < rows_ * cols_) {
        throw Error(std::format(
            "Inputted buffer needs to be at least size {} x {}", rows_, cols_));
    }

    const FrameHeader header{
        .rows = static_cast<std::uint32_t>(rows_),
        .cols = static_cast<std::uint32_t>(cols_),
        .frame_id = id,
        .timestamp_us =
            static_cast<std::uint64_t>(std::llround(timestamp_s * 1e6)),
    };

    // as_bytes derives the byte count from the type, so neither the header size
    // nor the frame size is ever spelled out by hand.
    write_bytes(out_, std::as_bytes(std::span{&header, 1}));
    write_bytes(out_, std::as_bytes(pixels.first(rows_ * cols_)));
    throw_if_failed(out_, "frame");
}

} // namespace opir
