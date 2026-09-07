#pragma once

#include <cstdint>
#include <mdspan>

namespace opir {
/// One detector sample: 16-bit unsigned counts, as the sensor delivers them.
using Pixel = std::uint16_t;

/// Index of a frame in a sequence. Multiply by SceneParams::dt for seconds.
using FrameId = std::uint32_t;

/// Identifies a target across frames.
using TargetId = std::uint32_t;

///  A span to navigate a frames contents by row and col
using FrameSpan = std::mdspan<const Pixel, std::dextents<std::size_t, 2>>;

} // namespace opir
