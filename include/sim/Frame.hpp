#include <cstddef>
#include <sys/types.h>
#include <vector>

namespace scitec {

template <typename T> struct FrameHeader {
    u_int16_t frame_count = 0;
    std::size_t rows;
    std::size_t columns;
    std::vector<T> raw;
};

} // namespace scitec