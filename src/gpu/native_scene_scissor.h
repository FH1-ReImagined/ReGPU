#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include "native_scene_size.h"
namespace regpu::native_scene {
// Mirrors the existing centred-scissor policy. Squeeze is the shader's
// actual vwide.z setting; this does not classify a draw as HUD.
inline int32_t CenteredScissorX(int32_t x,double guestWidth,double hostPerGuest,double squeeze) noexcept {
 return int32_t(std::lround(guestWidth*hostPerGuest/2.0+(double(x)-guestWidth/2.0)*hostPerGuest*squeeze));
}

struct ScissorRect {
    int32_t left, top, right, bottom;
};

// For a full, origin-zero viewport, select exactly the destination pixel centers
// whose inverse mapping lies in the stock half-open scissor.
inline bool MapPrivateScissor(ScissorRect stock, Size source, Size output, ScissorRect& mapped) noexcept {
    constexpr auto maxEdge = uint32_t(std::numeric_limits<int32_t>::max());
    if (!source.width || !source.height || !output.width || !output.height ||
        source.width > maxEdge || source.height > maxEdge ||
        output.width > maxEdge || output.height > maxEdge ||
        stock.left < 0 || stock.top < 0 || stock.right <= stock.left || stock.bottom <= stock.top ||
        uint32_t(stock.right) > source.width || uint32_t(stock.bottom) > source.height)
        return false;
    // Exact integer form of PixelCenterEdge(edge * dst / src), including half-pixel ties.
    const auto edge = [](uint32_t value, uint32_t src, uint32_t dst) {
        return int32_t((2ull * value * dst + src - 1) / (2ull * src));
    };
    const ScissorRect rect{edge(stock.left, source.width, output.width),
                           edge(stock.top, source.height, output.height),
                           edge(stock.right, source.width, output.width),
                           edge(stock.bottom, source.height, output.height)};
    if (rect.left == rect.right || rect.top == rect.bottom) return false;
    mapped = rect;
    return true;
}
}
