#pragma once

#include "core/Types.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace opir {

/// Spelled as characters rather than a packed integer so that it reads as
/// "OPIR" in a hex dump whichever way round the machine stores integers.
inline constexpr std::array<char, 4> kFrameMagic{'O', 'P', 'I', 'R'};

/// Bumped whenever the layout below changes in a way an old reader would
/// misinterpret. A reader refuses anything it does not recognise rather than
/// guessing.
inline constexpr std::uint16_t kFrameVersion = 1;

/// Larger than any plausible focal plane, and small enough that a garbage
/// dimension cannot talk a reader into a huge allocation.
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

static_assert(sizeof(FrameHeader) == 32,
              "the header is a wire format; its size is part of it");
static_assert(std::is_trivially_copyable_v<FrameHeader>,
              "the header is written as raw bytes");
static_assert(std::has_unique_object_representations_v<FrameHeader>,
              "implicit padding would put uninitialised bytes in the file");

} // namespace opir
