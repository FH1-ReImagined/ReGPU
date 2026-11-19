#pragma once

#include <cstdint>

// Pure validation and geometry for a future native scene/HUD split. This does
// not choose the scene boundary, allocate a texture, or select a filter.
namespace regpu::native_scene {

struct Size {
    uint32_t width, height;
};

enum class Transfer { Invalid, Copy, Resample };

constexpr Transfer TransferFor(Size scene, Size output)
{
    if (!scene.width || !scene.height || !output.width || !output.height)
        return Transfer::Invalid;
    return scene.width == output.width && scene.height == output.height ? Transfer::Copy
                                                                         : Transfer::Resample;
}

// Logical rectangles start at the top left of distinct physical resources.
// The caller must prove that even separately created resources do not alias.
constexpr bool BlitExtentsValid(Size scene, Size output, Size sourceAllocation,
                                Size destinationAllocation, bool nonAliasing)
{
    return nonAliasing && TransferFor(scene, output) != Transfer::Invalid &&
           scene.width <= sourceAllocation.width && scene.height <= sourceAllocation.height &&
           output.width <= destinationAllocation.width && output.height <= destinationAllocation.height;
}

} // namespace regpu::native_scene
