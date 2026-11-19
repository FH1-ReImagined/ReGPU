#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

// Standalone scene-colour resampling. There is deliberately no renderer call
// site: the caller must first prove which scene image belongs to which HUD.
namespace regpu::native_scene {

struct BlitRequest {
    ID3D12Resource* source = nullptr;
    ID3D12Resource* destination = nullptr;
    UINT sourceWidth = 0, sourceHeight = 0;
    UINT destinationWidth = 0, destinationHeight = 0;
    D3D12_RESOURCE_STATES sourceState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES destinationState = D3D12_RESOURCE_STATE_COMMON;
    // The caller must prove the resources do not overlap in their underlying
    // heaps, including when they are different placed-resource objects.
    bool nonAliasing = false;
};

class ColorBlit {
public:
    // Device-specific. Keep this object alive until all recorded work completes.
    bool Initialize(ID3D12Device* device);

    // Creates a separate single-sample renderable target in shader-read state.
    // On failure, output is left null and no renderer state is changed.
    static bool CreateTarget(ID3D12Device* device, UINT width, UINT height,
                             DXGI_FORMAT format, Microsoft::WRL::ComPtr<ID3D12Resource>& output);

    // Allocate a fresh committed target and record conversion before publishing it.
    // Failure leaves output unchanged. The caller owns the original source and
    // descriptor slots through the frame fence and changes phase only on true.
    bool CreateAndRecord(ID3D12Device* device, ID3D12GraphicsCommandList* list,
                         ID3D12DescriptorHeap* srvHeap, UINT srvIndex,
                         ID3D12DescriptorHeap* rtvHeap, UINT rtvIndex,
                         const BlitRequest& request,
                         std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& frameRetained,
                         Microsoft::WRL::ComPtr<ID3D12Resource>& output) const noexcept;

    // Caller owns unique descriptor indices for this frame until its fence
    // completes and passes the frame's fence-retained resource vector. Resource
    // states are restored; graphics state/heaps are changed and MUST be rebound
    // or invalidated by the caller before any subsequent guest draw.
    bool Record(ID3D12Device* device, ID3D12GraphicsCommandList* list,
                ID3D12DescriptorHeap* srvHeap, UINT srvIndex,
                ID3D12DescriptorHeap* rtvHeap, UINT rtvIndex,
                const BlitRequest& request,
                std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& frameRetained) const;

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> rgba8_, rgba16_;
};

} // namespace regpu::native_scene
