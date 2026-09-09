
#pragma once

#include "core/ParseError.hpp"
#include "sim/SceneSimulator.hpp"
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

namespace opir {

class TruthReader {
    std::ifstream in_;
    std::vector<TruthRecord>
        buffer_; // allocated on the first frame, then reused
    size_t rows_ = 0, cols_ = 0;

  public:
    /// The stream carries its own shape, so no dimensions are needed here.
    explicit TruthReader(std::filesystem::path path);

    ///
    /// Reads the next frame, or reports why it could not.
    ///
    /// \return EndOfStream once the file is exhausted, which is the normal way
    ///         a read loop finishes rather than a failure.
    ///
    std::expected<std::span<const TruthRecord>, ParseError> next();
};

} // namespace opir
