#pragma once

#include "core/Types.hpp"
#include <cstddef>
#include <sys/types.h>
#include <vector>

namespace opir {

struct FrameHeader {
    u_int32_t frame_id = 0;
    double timestamp_us;
    std::size_t rows;
    std::size_t cols;
    std::vector<Pixel> raw;
};

} // namespace opir
