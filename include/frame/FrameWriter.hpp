#pragma once

#include "core/Types.hpp"
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>

namespace opir {

class FrameWriter {
    std::ofstream out_;
    size_t rows_ = 0, cols_ = 0;

  public:
    FrameWriter(std::filesystem::path path, size_t rows, size_t cols);
    void write_frame(std::span<const Pixel> pixels);
};

} // namespace opir
