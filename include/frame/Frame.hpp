#pragma once

#include <cstddef>
#include <sys/types.h>
#include <vector>
#include "core/Types.hpp"

namespace opir {

struct FrameHeader {
    u_int32_t frame_id = 0;
    std::size_t rows;
    std::size_t columns;
    std::vector<Pixel> raw;
};

} // namespace opir
