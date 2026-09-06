#include "frame/FrameWriter.hpp"
#include "core/Error.hpp"
#include "frame/Frame.hpp"
#include <cstddef>
#include <format>
#include <ios>

namespace opir {

FrameWriter::FrameWriter(std::filesystem::path path, size_t rows, size_t cols)
    : out_{path, std::ios::binary}, rows_{rows}, cols_{cols} {
    if (!out_) {
        throw Error("Framewriter is cannot be initialized with null filepath");
    }
}

void FrameWriter::write_frame(std::span<const Pixel> pixels) {
    if (!out_) {
        throw Error("Trying to write with null filepath.");
    }

    if (pixels.size() < rows_ * cols_) {
        throw Error(std::format(
            "Inputted buffer needs to be at least size {} x {}", rows_, cols_));
    }
    out_.write(reinterpret_cast<const char *>(pixels.data()),
               static_cast<std::streamsize>(rows_ * cols_ * sizeof(Pixel)));
}
} // namespace opir
