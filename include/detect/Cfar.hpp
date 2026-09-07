#pragma once

#include "core/Types.hpp"

#include <cstdint>
#include <span>

namespace opir {

///
/// Tuning for the constant-false-alarm-rate threshold: the geometry of the
/// reference ring, and how far above its noise a pixel has to sit.
///
struct CfarParams {
    int guard = 3;  // guard band half-width, ~2-3x psf_sigma
    int ref = 6;    // outer half-width; ref cells lie between guard and ref
    double k = 5.0; // threshold in sigmas
};

void cfar_threshold(FrameSpan px, const CfarParams &p, std::span<uint8_t> mask,
                    std::span<float> bg);
} // namespace opir
