#pragma once
#include <cstdint>
namespace regpu::native_scene {
struct AlternateViewEvidence {
 bool explicitPhase, output16x9, msaaScaleEnabled, textureless, depthUsed, depthOnly;
 uint32_t pitch, mode, primitive, colorMask, colorKey, colorNative, depthKey, depthNative;
 uint32_t scissorTL,scissorBR,windowOffset;
 float vx,vy,ox,oy;
};
// Only the native-primary depth-only full rectangle whose existing special
// MSAA path supplies 2x on both axes. No raw-pitch-only promotion.
constexpr bool IsKnownPrimaryRectangle(const AlternateViewEvidence& e) noexcept {
 return e.explicitPhase && e.output16x9 && e.msaaScaleEnabled && e.textureless && e.depthUsed && e.depthOnly &&
  e.pitch==640 && e.mode==2 && e.primitive==8 && (e.colorMask&15)==0 &&
  (e.colorNative>>2)==e.colorKey && (e.colorNative&3)==0 &&
  (e.depthNative>>2)==e.depthKey && (e.depthNative&3)==0 &&
  e.scissorTL==0 && e.scissorBR==0x20002000u && e.windowOffset==0 &&
  e.vx==640.0f && e.vy==-360.0f && e.ox==640.0f && e.oy==360.0f;
}
}
