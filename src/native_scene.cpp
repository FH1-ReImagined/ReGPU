#include "regpu/native_scene.h"
#include "regpu/regpu.h"
#include <atomic>
namespace regpu {
namespace { constexpr uint64_t kStarted=uint64_t(1)<<63; std::atomic<uint64_t> sceneState{0}; }
NativeSceneResult SetNativeSceneSize(uint32_t width,uint32_t height) noexcept {
    if ((!width)!=(!height) || width>16384 || height>5760) return NativeSceneResult::InvalidPair;
    if (width && GetConfig().upscaler!=Config::Upscaler::Off) return NativeSceneResult::IncompatibleUpscaler;
    const uint64_t pair=(uint64_t(width)<<32)|height;
    uint64_t state=sceneState.load(std::memory_order_acquire);
    do { if (state&kStarted) return NativeSceneResult::AlreadyStarted; }
    while (!sceneState.compare_exchange_weak(state,pair,std::memory_order_acq_rel));
    return NativeSceneResult::Accepted;
}
NativeSceneSize GetNativeSceneSize() noexcept {
    const uint64_t pair=sceneState.load(std::memory_order_acquire)&~kStarted;
    return {uint32_t(pair>>32),uint32_t(pair)};
}
namespace detail { void LockNativeSceneSize() noexcept { sceneState.fetch_or(kStarted,std::memory_order_acq_rel); } }
}
