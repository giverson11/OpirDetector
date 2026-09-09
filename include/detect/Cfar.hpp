#pragma once

#include "core/Types.hpp"

#include <cstdint>

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

///
/// Flags every pixel standing k sigmas above its own local background, and
/// hands back the noise model it used so a later stage can score a detection.
///
/// \param px
/// \param p
/// \param mask cleared in full; set only on the interior
/// \param bg local mean, written only on the interior
/// \param sg local sigma, written only on the interior
///
void cfar_threshold(FrameSpan px, const CfarParams &p, Plane<std::uint8_t> mask,
                    Plane<double> bg, Plane<double> sg);
} // namespace opir
