#pragma once

#include <cstddef>
#include <cstdint>
#include <mdspan>

namespace opir {
/// One detector sample: 16-bit unsigned counts, as the sensor delivers them.
using Pixel = std::uint16_t;

/// Index of a frame in a sequence. Multiply by SceneParams::dt for seconds.
using FrameId = std::uint32_t;

/// Identifies a target across frames.
using TargetId = std::uint32_t;

using TrackId = std::uint32_t;

///
/// What a two-dimensional view means everywhere in this project: dynamic
/// extents, size_t indices, row-major. Each layer names its own element types
/// on top of this rather than restating the convention.
///
template <class T> using Plane = std::mdspan<T, std::dextents<std::size_t, 2>>;

///  A span to navigate a frames contents by row and col
using FrameSpan = Plane<const Pixel>;

struct Detection {
    FrameId frame_id;
    double row, col;  // sub-pixel centroid
    double amplitude; // background-subtracted peak
    double snr;
};

} // namespace opir
