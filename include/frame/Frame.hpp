#pragma once

#include "core/Types.hpp"

#include <array>
#include <cstdint>

namespace opir {

inline constexpr std::array<char, 4> kFrameMagic{'O', 'P', 'I', 'R'};

inline constexpr std::uint16_t kFrameVersion = 1;

inline constexpr std::uint32_t kMaxFrameDim = 8192;

struct FrameHeader {
    std::array<char, 4> magic{kFrameMagic};
    std::uint16_t version{kFrameVersion};
    std::uint16_t reserved{0}; ///< must be zero; room to grow
    std::uint32_t rows{0};
    std::uint32_t cols{0};
    FrameId frame_id{0};
    std::uint32_t reserved2{0}; ///< must be zero; keeps timestamp_us aligned
    std::uint64_t timestamp_us{0};
};

} // namespace opir
