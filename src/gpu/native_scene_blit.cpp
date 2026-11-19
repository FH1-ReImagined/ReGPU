#include "native_scene_blit.h"
#include "native_scene_size.h"

#include <d3dcompiler.h>
#include <limits>

using Microsoft::WRL::ComPtr;

namespace regpu::native_scene {
namespace {
bool Supported(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_R16G16B16A16_FLOAT;
}

// Explicitly limited to ordinary single-purpose states; callers must resolve
// UAV, depth, present and copy-destination hazards before invoking the blit.
bool SupportedState(D3D12_RESOURCE_STATES state) {
    return state == D3D12_RESOURCE_STATE_RENDER_TARGET || state == D3D12_RESOURCE_STATE_COPY_SOURCE ||
           state == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE ||
           state == (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (before == after) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    list->ResourceBarrier(1, &b);
}
} // namespace

bool ColorBlit::Initialize(ID3D12Device* device) {
    if (!device || root_) return false;
    // New pipeline and root signature are built off to the side. A failed
    // compile/creation leaves the previous, usable object untouched.
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.Num32BitValues = 4;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC signature{};
    signature.NumParameters = 2;
    signature.pParameters = parameters;
    signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> serialized, error, vs, ps;
    if (FAILED(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1,
                                            &serialized, &error))) return false;
    ComPtr<ID3D12RootSignature> root;
    if (FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                            IID_PPV_ARGS(&root)))) return false;
    // Four exact Load operations, clamped at the source's logical bounds.
    // Pixel centres are mapped independently on X and Y, including downscale
    // and mixed-direction scaling. No FSR sharpening or gamma conversion.
    static constexpr char shader[] = R"(
Texture2D<float4> source : register(t0);
cbuffer Size : register(b0) { uint sw; uint sh; uint dw; uint dh; };
float4 vs(uint id : SV_VertexID) : SV_Position {
  float2 uv = float2((id << 1) & 2, id & 2);
  return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 ps(float4 pos : SV_Position) : SV_Target {
  float2 p = clamp(pos.xy * float2(sw, sh) / float2(dw, dh) - 0.5,
                   0.0, float2(sw - 1, sh - 1));
  int2 lo = int2(floor(p));
  int2 hi = min(lo + 1, int2(sw - 1, sh - 1));
  float2 t = frac(p);
  float4 a = lerp(source.Load(int3(lo, 0)), source.Load(int3(hi.x, lo.y, 0)), t.x);
  float4 b = lerp(source.Load(int3(lo.x, hi.y, 0)), source.Load(int3(hi, 0)), t.x);
  return lerp(a, b, t.y);
})";
    if (FAILED(D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vs, &error)) ||
        FAILED(D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "ps", "ps_5_0", 0, 0, &ps, &error)))
        return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
    p.pRootSignature = root.Get();
    p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    auto& blend = p.BlendState.RenderTarget[0];
    blend.SrcBlend = D3D12_BLEND_ONE; blend.DestBlend = D3D12_BLEND_ZERO; blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE; blend.DestBlendAlpha = D3D12_BLEND_ZERO; blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP; blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    p.SampleMask = (std::numeric_limits<UINT>::max)();
    p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    p.RasterizerState.DepthClipEnable = TRUE;
    p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    p.NumRenderTargets = 1;
    p.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> eight, sixteen;
    p.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (FAILED(device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&eight)))) return false;
    p.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (FAILED(device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&sixteen)))) return false;
    root_ = std::move(root); rgba8_ = std::move(eight); rgba16_ = std::move(sixteen);
    return true;
}

bool ColorBlit::CreateTarget(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format,
                             ComPtr<ID3D12Resource>& output) {
    output.Reset();
    if (!device || !width || !height || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || !Supported(format)) return false;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1;
    desc.MipLevels = 1; desc.Format = format; desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    const auto read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    return SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, read,
                                                      nullptr, IID_PPV_ARGS(&output)));
}

bool ColorBlit::CreateAndRecord(ID3D12Device* device, ID3D12GraphicsCommandList* list,
                                ID3D12DescriptorHeap* srvHeap, UINT srvIndex,
                                ID3D12DescriptorHeap* rtvHeap, UINT rtvIndex,
                                const BlitRequest& request,
                                std::vector<ComPtr<ID3D12Resource>>& frameRetained,
                                ComPtr<ID3D12Resource>& output) const noexcept {
    try {
        if (!request.source || request.destination || !device || !list || !srvHeap || !rtvHeap)
            return false;
        const auto sourceDesc = request.source->GetDesc();
        ComPtr<ID3D12Resource> candidate;
        if (!CreateTarget(device, request.destinationWidth, request.destinationHeight,
                          sourceDesc.Format, candidate)) return false;
        // A new committed allocation cannot overlap the existing source.
        BlitRequest staged = request;
        staged.destination = candidate.Get();
        staged.destinationState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        staged.nonAliasing = true;
        // Reserve before recording: allocations must not throw after GPU commands begin.
        frameRetained.reserve(frameRetained.size() + 2);
        if (!Record(device, list, srvHeap, srvIndex, rtvHeap, rtvIndex, staged, frameRetained))
            return false;
        output = std::move(candidate);
        return true;
    } catch (...) { return false; }
}

bool ColorBlit::Record(ID3D12Device* device, ID3D12GraphicsCommandList* list,
                       ID3D12DescriptorHeap* srvHeap, UINT srvIndex,
                       ID3D12DescriptorHeap* rtvHeap, UINT rtvIndex, const BlitRequest& q,
                       std::vector<ComPtr<ID3D12Resource>>& frameRetained) const {
    if (!device || !list || !srvHeap || !rtvHeap || !root_ || !rgba8_ || !rgba16_ ||
        !q.source || !q.destination || q.source == q.destination ||
        !SupportedState(q.sourceState) || !SupportedState(q.destinationState)) return false;
    const auto src = q.source->GetDesc(), dst = q.destination->GetDesc();
    if (src.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || dst.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        src.DepthOrArraySize != 1 || dst.DepthOrArraySize != 1 || src.SampleDesc.Count != 1 || dst.SampleDesc.Count != 1 ||
        !Supported(src.Format) || src.Format != dst.Format || !(dst.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ||
        (src.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ||
        src.Width > (std::numeric_limits<uint32_t>::max)() ||
        dst.Width > (std::numeric_limits<uint32_t>::max)() ||
        !BlitExtentsValid({q.sourceWidth, q.sourceHeight}, {q.destinationWidth, q.destinationHeight},
                          {uint32_t(src.Width), src.Height}, {uint32_t(dst.Width), dst.Height}, q.nonAliasing))
        return false;
    const auto srvDesc = srvHeap->GetDesc(), rtvDesc = rtvHeap->GetDesc();
    if (srvDesc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
        !(srvDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) || srvIndex >= srvDesc.NumDescriptors ||
        rtvDesc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_RTV || rtvIndex >= rtvDesc.NumDescriptors) return false;
    ComPtr<ID3D12Device> objectDevice;
    for (ID3D12DeviceChild* object : {static_cast<ID3D12DeviceChild*>(q.source),
                                     static_cast<ID3D12DeviceChild*>(q.destination),
                                     static_cast<ID3D12DeviceChild*>(srvHeap),
                                     static_cast<ID3D12DeviceChild*>(rtvHeap),
                                     static_cast<ID3D12DeviceChild*>(list),
                                     static_cast<ID3D12DeviceChild*>(root_.Get())}) {
        objectDevice.Reset();
        if (FAILED(object->GetDevice(IID_PPV_ARGS(&objectDevice))) || objectDevice.Get() != device)
            return false;
    }

    // The source, destination and unique descriptor slots must outlive GPU work.
    frameRetained.emplace_back(q.source);
    frameRetained.emplace_back(q.destination);
    auto srvCpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
    auto srvGpu = srvHeap->GetGPUDescriptorHandleForHeapStart();
    const auto srvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    srvCpu.ptr += SIZE_T(srvIndex) * srvStride; srvGpu.ptr += UINT64(srvIndex) * srvStride;
    auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += SIZE_T(rtvIndex) * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = src.Format; view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(q.source, &view, srvCpu);
    device->CreateRenderTargetView(q.destination, nullptr, rtv);
    Transition(list, q.source, q.sourceState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(list, q.destination, q.destinationState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12DescriptorHeap* heaps[] = {srvHeap};
    list->SetDescriptorHeaps(1, heaps);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    list->SetGraphicsRootSignature(root_.Get());
    list->SetPipelineState(src.Format == DXGI_FORMAT_R8G8B8A8_UNORM ? rgba8_.Get() : rgba16_.Get());
    list->SetGraphicsRootDescriptorTable(0, srvGpu);
    const UINT size[] = {q.sourceWidth, q.sourceHeight, q.destinationWidth, q.destinationHeight};
    list->SetGraphicsRoot32BitConstants(1, 4, size, 0);
    const D3D12_VIEWPORT viewport{0, 0, float(q.destinationWidth), float(q.destinationHeight), 0, 1};
    const D3D12_RECT rect{0, 0, LONG(q.destinationWidth), LONG(q.destinationHeight)};
    list->RSSetViewports(1, &viewport); list->RSSetScissorRects(1, &rect);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
    Transition(list, q.destination, D3D12_RESOURCE_STATE_RENDER_TARGET, q.destinationState);
    Transition(list, q.source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, q.sourceState);
    return true;
}
} // namespace regpu::native_scene
