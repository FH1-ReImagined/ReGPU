#pragma once
#include <cmath>
#include <cstdint>
namespace regpu::native_scene {
// First integer pixel index whose center is at or beyond a continuous edge.
// Adjacent half-open spans therefore share one quantized edge.
inline int32_t PixelCenterEdge(double edge) noexcept { return int32_t(std::ceil(edge-0.5)); }
}
