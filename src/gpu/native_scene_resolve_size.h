#pragma once
#include <algorithm>
#include <cstdint>
namespace regpu::native_scene {
enum class ResolveSizing { Floor, Round, ExactMain };
constexpr ResolveSizing SelectResolveSizing(bool overrideRound,bool explicitPhase,bool rawWide,
 uint32_t width,uint32_t height,uint32_t passWidth,uint32_t passHeight,float passScale) noexcept {
 if (overrideRound) return ResolveSizing::Round;
 if (explicitPhase && rawWide && width==1280 && height==720 && passWidth==1280 && passHeight==720 && passScale==1.0f)
  return ResolveSizing::ExactMain;
 return ResolveSizing::Floor;
}
inline uint32_t ResolveDimension(ResolveSizing mode,uint32_t guest,double scale,uint32_t rounded,uint32_t exact) noexcept {
 if(mode==ResolveSizing::Round) return rounded;
 if(mode==ResolveSizing::ExactMain) return exact;
 return std::max(1u,uint32_t(double(guest)*scale+1e-3));
}
}
