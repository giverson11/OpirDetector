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

    ///
    /// Writes one frame: a FrameHeader followed by rows * cols pixels.
    ///
    /// The id and timestamp are passed in rather than counted internally so
    /// that they agree with the truth table written alongside; nothing here
    /// invents them.
    ///
    /// \param id
    /// \param timestamp_s seconds since the start of the capture
    /// \param pixels at least rows * cols samples; any surplus is ignored
    ///
    void write_frame(FrameId id, double timestamp_s,
                     std::span<const Pixel> pixels);
};

} // namespace opir
