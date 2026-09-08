#pragma once

#include "core/Types.hpp"

#include <cstddef>
#include <numeric>
#include <vector>

namespace opir::test {

/// first, first + 1, first + 2, ... so that every pixel is distinguishable
/// from every other and none of them is the zero a buffer would hold if it
/// were never written.
inline std::vector<Pixel> ramp(std::size_t count, Pixel first = 1) {
    std::vector<Pixel> values(count);
    std::iota(values.begin(), values.end(), first);
    return values;
}

} // namespace opir::test
