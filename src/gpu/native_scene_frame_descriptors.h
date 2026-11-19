#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
namespace regpu::native_scene {
// One instance per in-flight frame. The owner retains this object until the
// signaled queue fence completes and never calls Cancel after submitting.
class FrameBlitDescriptors {
public:
 bool Initialize(ID3D12Device* device) noexcept {
  if (!device || srv_ || rtv_) return false;
  D3D12_DESCRIPTOR_HEAP_DESC d{}; d.NumDescriptors=1;
  d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; d.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv,rtv;
  if (FAILED(device->CreateDescriptorHeap(&d,IID_PPV_ARGS(&srv)))) return false;
  d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; d.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  if (FAILED(device->CreateDescriptorHeap(&d,IID_PPV_ARGS(&rtv)))) return false;
  srv_=std::move(srv); rtv_=std::move(rtv); return true;
 }
 bool Begin(uint64_t completed) noexcept {
  if (!srv_ || !rtv_ || active_ || completed==UINT64_MAX || completed<retire_) return false;
  active_=true; return true;
 }
 bool Submitted(uint64_t signal) noexcept {
  if (!active_ || !signal || signal<=retire_ || signal==UINT64_MAX) return false;
  retire_=signal; active_=false; return true;
 }
 void CancelUnsubmitted() noexcept { active_=false; }
 ID3D12DescriptorHeap* srv() const noexcept { return active_ ? srv_.Get() : nullptr; }
 ID3D12DescriptorHeap* rtv() const noexcept { return active_ ? rtv_.Get() : nullptr; }
private:
 Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_,rtv_;
 uint64_t retire_=0; bool active_=false;
};
}
