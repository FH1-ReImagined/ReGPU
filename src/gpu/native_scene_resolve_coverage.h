#pragma once
#include <cstdint>
#include <array>
#include <algorithm>
namespace regpu::native_scene {
// Coverage of actually recorded full-width writes to one exact resource/frame.
// Unsupported partial-width or failed writes invalidate all prior evidence.
class ResolveCoverage {
public:
 void Reset() noexcept { *this={}; }
 bool Record(uintptr_t resource,uint64_t frame,uint32_t base,uint32_t width,uint32_t height,
             uint32_t left,uint32_t top,uint32_t right,uint32_t bottom,bool recorded) noexcept {
  if (!resource || !width || !height) { Reset(); return false; }
  if (resource_!=resource || frame_!=frame || base_!=base || width_!=width || height_!=height) {
   Reset();resource_=resource;frame_=frame;base_=base;width_=width;height_=height;
  }
  if (!recorded || left!=0 || right!=width || top>=bottom || bottom>height) { count_=0;return false; }
  for (uint32_t i=0;i<count_;) {
   if (bottom<ranges_[i].first || top>ranges_[i].second) { ++i;continue; }
   top=std::min(top,ranges_[i].first);bottom=std::max(bottom,ranges_[i].second);ranges_[i]=ranges_[--count_];
  }
  if (count_==ranges_.size()) { count_=0;return false; }
  ranges_[count_++]={top,bottom};return true;
 }
 bool Complete(uintptr_t resource,uint64_t frame,uint32_t base,uint32_t width,uint32_t height) const noexcept {
  return resource && resource_==resource && frame_==frame && base_==base && width_==width && height_==height && count_==1 && ranges_[0].first==0 && ranges_[0].second==height;
 }
private:
 uintptr_t resource_=0;uint64_t frame_=0;uint32_t base_=0,width_=0,height_=0,count_=0;
 std::array<std::pair<uint32_t,uint32_t>,16> ranges_{};
};
}
