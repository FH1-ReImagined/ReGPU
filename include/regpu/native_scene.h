#pragma once
#include <cstdint>
namespace regpu {
// A restart-required scene raster request. The independent presentation size
// is controlled by the existing output settings.
struct NativeSceneSize { uint32_t width=0, height=0; };
enum class NativeSceneResult { Accepted, InvalidPair, IncompatibleUpscaler, AlreadyStarted };
// Both zero restores the legacy path. Both positive must fit the conservative
// backing envelope; actual allocation/device support is checked by renderer.
// This must be called before Start(); a failure leaves the previous pair intact.
NativeSceneResult SetNativeSceneSize(uint32_t width,uint32_t height) noexcept;
NativeSceneSize GetNativeSceneSize() noexcept;
namespace detail { void LockNativeSceneSize() noexcept; }
}
