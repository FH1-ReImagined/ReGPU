#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

namespace regpu::native_scene {
// Private 1x depth+stencil seed. No renderer routing or attachment ownership is implied.
// Caller must establish source freshness, logical extent, no overlap, unique descriptor
// slots, and fence lifetime. Both graphics state and descriptor heaps must be rebound.
class DepthStencilBitSeed {
public:
 bool Initialize(ID3D12Device* device) noexcept;
 bool CreateAndRecord(ID3D12Device* device, ID3D12GraphicsCommandList* list,
   ID3D12DescriptorHeap* srvHeap, UINT firstSrv, ID3D12DescriptorHeap* dsvHeap, UINT dsvIndex,
   ID3D12Resource* source, UINT sourceWidth, UINT sourceHeight, UINT destWidth, UINT destHeight,
   std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& frameRetained,
   Microsoft::WRL::ComPtr<ID3D12Resource>& output) const noexcept;
private:
 Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
 Microsoft::WRL::ComPtr<ID3D12PipelineState> depthPipeline_, stencilPipelines_[8];
};
}
