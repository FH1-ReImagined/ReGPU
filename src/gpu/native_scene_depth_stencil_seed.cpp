#include "native_scene_depth_stencil_seed.h"
#include <d3dcompiler.h>
#include <limits>
#include <utility>
using Microsoft::WRL::ComPtr;
namespace regpu::native_scene {
namespace {
void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
 D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};list->ResourceBarrier(1,&b);
}
}
bool DepthStencilSeed::Initialize(ID3D12Device* device) noexcept {
 try {
  if(!device || root_ || pipeline_) return false;
  D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
  if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,&options,sizeof(options))) || !options.PSSpecifiedStencilRefSupported) return false;
  D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=2;
  D3D12_ROOT_PARAMETER parameters[2]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[0].DescriptorTable={1,&range};parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[1].Constants.Num32BitValues=4;parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC signature{};signature.NumParameters=2;signature.pParameters=parameters;signature.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  ComPtr<ID3DBlob> serialized,error,vs,ps;if(FAILED(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error))) return false;
  ComPtr<ID3D12RootSignature> root;if(FAILED(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root)))) return false;
  static constexpr char shader[]=R"(
Texture2D<float> depthSource:register(t0); Texture2D<uint> stencilSource:register(t1);
cbuffer Size:register(b0) { uint sw; uint sh; uint dw; uint dh; };
float4 vs(uint id:SV_VertexID):SV_Position { float2 p=float2((id<<1)&2,id&2); return float4(p*float2(2,-2)+float2(-1,1),0,1); }
struct Result { float depth:SV_Depth; uint stencil:SV_StencilRef; };
Result ps(float4 p:SV_Position) { int2 xy=int2(min((uint)(p.x*sw/dw),sw-1),min((uint)(p.y*sh/dh),sh-1)); Result r; r.depth=depthSource.Load(int3(xy,0));r.stencil=stencilSource.Load(int3(xy,0));return r; }
)";
  if(FAILED(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"vs","vs_5_1",0,0,&vs,&error)) || FAILED(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"ps","ps_5_1",0,0,&ps,&error))) return false;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};pd.SampleMask=UINT_MAX;
  auto& blend=pd.BlendState.RenderTarget[0];blend.SrcBlend=D3D12_BLEND_ONE;blend.DestBlend=D3D12_BLEND_ZERO;blend.BlendOp=D3D12_BLEND_OP_ADD;blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
  pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
  auto& ds=pd.DepthStencilState;ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;ds.StencilEnable=TRUE;ds.StencilReadMask=255;ds.StencilWriteMask=255;
  D3D12_DEPTH_STENCILOP_DESC face{};face.StencilFailOp=face.StencilDepthFailOp=face.StencilPassOp=D3D12_STENCIL_OP_REPLACE;face.StencilFunc=D3D12_COMPARISON_FUNC_ALWAYS;ds.FrontFace=ds.BackFace=face;
  pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.DSVFormat=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;pd.SampleDesc.Count=1;
  ComPtr<ID3D12PipelineState> pipeline;if(FAILED(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pipeline)))) return false;
  root_=std::move(root);pipeline_=std::move(pipeline);return true;
 } catch(...) {return false;}
}
bool DepthStencilSeed::CreateAndRecord(ID3D12Device* device,ID3D12GraphicsCommandList* list,ID3D12DescriptorHeap* srvHeap,UINT firstSrv,ID3D12DescriptorHeap* dsvHeap,UINT dsvIndex,ID3D12Resource* source,UINT sw,UINT sh,UINT dw,UINT dh,std::vector<ComPtr<ID3D12Resource>>& retained,ComPtr<ID3D12Resource>& output) const noexcept {
 try {
  if(!device || !list || !srvHeap || !dsvHeap || !source || !root_ || !pipeline_ || !sw || !sh || !dw || !dh || dw>D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || dh>D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION) return false;
  const auto src=source->GetDesc(); const auto srv=srvHeap->GetDesc(); const auto dsv=dsvHeap->GetDesc();
  if(src.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || src.Format!=DXGI_FORMAT_R32G8X24_TYPELESS || src.SampleDesc.Count!=1 || src.DepthOrArraySize!=1 || src.MipLevels!=1 || !(src.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) || (src.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) || sw>src.Width || sh>src.Height || srv.Type!=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || !(srv.Flags&D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) || firstSrv>=srv.NumDescriptors || srv.NumDescriptors-firstSrv<2 || dsv.Type!=D3D12_DESCRIPTOR_HEAP_TYPE_DSV || dsvIndex>=dsv.NumDescriptors) return false;
  ComPtr<ID3D12Device> owner;for(ID3D12DeviceChild* object:{static_cast<ID3D12DeviceChild*>(list),static_cast<ID3D12DeviceChild*>(source),static_cast<ID3D12DeviceChild*>(srvHeap),static_cast<ID3D12DeviceChild*>(dsvHeap),static_cast<ID3D12DeviceChild*>(root_.Get())}) {owner.Reset();if(FAILED(object->GetDevice(IID_PPV_ARGS(&owner))) || owner.Get()!=device) return false;}
  D3D12_RESOURCE_DESC desc=src;desc.Width=dw;desc.Height=dh;D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;clear.DepthStencil={1.0f,0};D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
  ComPtr<ID3D12Resource> dest;if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&dest)))) return false;
  retained.reserve(retained.size()+2);retained.emplace_back(source);retained.emplace_back(dest);
  auto cpu=srvHeap->GetCPUDescriptorHandleForHeapStart();const auto stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);cpu.ptr+=SIZE_T(firstSrv)*stride;auto gpu=srvHeap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(firstSrv)*stride;
  D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Texture2D.MipLevels=1;device->CreateShaderResourceView(source,&sv,cpu);
  cpu.ptr+=stride;sv.Format=DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;sv.Shader4ComponentMapping=D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(1,1,1,1);sv.Texture2D.PlaneSlice=1;device->CreateShaderResourceView(source,&sv,cpu);
  auto out=dsvHeap->GetCPUDescriptorHandleForHeapStart();out.ptr+=SIZE_T(dsvIndex)*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);D3D12_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;dv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;device->CreateDepthStencilView(dest.Get(),&dv,out);
  Transition(list,source,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);ID3D12DescriptorHeap* heaps[]={srvHeap};list->SetDescriptorHeaps(1,heaps);list->OMSetRenderTargets(0,nullptr,FALSE,&out);list->SetGraphicsRootSignature(root_.Get());list->SetPipelineState(pipeline_.Get());list->SetGraphicsRootDescriptorTable(0,gpu);const UINT dims[]={sw,sh,dw,dh};list->SetGraphicsRoot32BitConstants(1,4,dims,0);
  const D3D12_VIEWPORT vp{0,0,float(dw),float(dh),0,1};const D3D12_RECT rect{0,0,LONG(dw),LONG(dh)};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&rect);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->DrawInstanced(3,1,0,0);Transition(list,source,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);output=std::move(dest);return true;
 } catch(...) {return false;}
}
}
