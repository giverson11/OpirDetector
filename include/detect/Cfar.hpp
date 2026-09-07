#include "Params.hpp"
#include "core/Types.hpp"

namespace opir {

void cfar_threshold(FrameSpan px, const CfarParams &p, std::span<uint8_t> mask,
                    std::span<float> bg);
} // namespace opir
