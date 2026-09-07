#pragma once

#include "core/ParseError.hpp"
#include "core/Types.hpp"
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <vector>

namespace opir {

/// One frame from the stream. `px` views the reader's own buffer, which the
/// next call to next() overwrites, so copy anything that has to outlive the
/// iteration.
struct FrameView {
    FrameId id;
    double t;
    FrameSpan px;
};

class FrameReader {
    std::ifstream in_;
    std::vector<Pixel> buffer_; // allocated on the first frame, then reused
    size_t rows_ = 0, cols_ = 0;

  public:
    /// The stream carries its own shape, so no dimensions are needed here.
    explicit FrameReader(std::filesystem::path path);

    ///
    /// Reads the next frame, or reports why it could not.
    ///
    /// @return EndOfStream once the file is exhausted, which is the normal way
    ///         a read loop finishes rather than a failure.
    ///
    std::expected<FrameView, ParseError> next();
};

} // namespace opir
