#pragma once

#include "core/ParseError.hpp"
#include "core/Types.hpp"
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <vector>

namespace opir {

struct FrameView {
    FrameId id;
    double t;
    FrameSpan px;
};

class FrameReader {
    std::ifstream in_;
    std::vector<Pixel> buffer_; // allocated once, reused
    size_t rows_ = 0, cols_ = 0;

  public:
    FrameReader(std::filesystem::path path, size_t rows, size_t cols);
    std::expected<FrameView, ParseError> next();
};

} // namespace opir
