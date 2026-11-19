#include "../src/gpu/native_scene_depth_stencil_seed.h"
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using Microsoft::WRL::ComPtr;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
#define HR(x) CHECK(SUCCEEDED(x))
static void transition(ID3D12GraphicsCommandList* l, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
 D3D12_RESOURCE_BARRIER t{}; t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
 t.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b}; l->ResourceBarrier(1,&t);
}
static bool compile(const char* code,const char* target,ComPtr<ID3DBlob>& out) {
 ComPtr<ID3DBlob> error; HRESULT hr=D3DCompile(code,std::strlen(code),nullptr,nullptr,nullptr,"main",target,D3DCOMPILE_ENABLE_STRICTNESS,0,&out,&error);
 if(FAILED(hr)) { if(error) std::printf("HLSL compile failed: %s\n",(const char*)error->GetBufferPointer()); return false; } return true;
}
int main() {
 ComPtr<ID3D12Debug> debug; const bool debugOn=SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))); if(debugOn) debug->EnableDebugLayer();
 ComPtr<IDXGIFactory4> factory; HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory))); ComPtr<IDXGIAdapter> warp; HR(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
 ComPtr<ID3D12Device> dev; HR(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev)));
 D3D12_FEATURE_DATA_D3D12_OPTIONS options{}; HR(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,&options,sizeof(options)));
 if(!options.PSSpecifiedStencilRefSupported) { std::puts("SKIP: WARP reports no pixel shader stencil reference support"); return 77; }
 ComPtr<ID3D12InfoQueue> info; if(debugOn) HR(dev.As(&info));
 D3D12_COMMAND_QUEUE_DESC q{}; q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT; ComPtr<ID3D12CommandQueue> queue; HR(dev->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
 ComPtr<ID3D12CommandAllocator> alloc; HR(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
 ComPtr<ID3D12GraphicsCommandList> list; HR(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list)));
 D3D12_HEAP_PROPERTIES hp{}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
 D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width=4; desc.Height=4; desc.DepthOrArraySize=1; desc.MipLevels=1;
 desc.Format=DXGI_FORMAT_R32G8X24_TYPELESS; desc.SampleDesc.Count=1; desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
 D3D12_CLEAR_VALUE cv{}; cv.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT; cv.DepthStencil={1.0f,0};
 ComPtr<ID3D12Resource> src,dst; HR(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&cv,IID_PPV_ARGS(&src)));
 desc.Height=2; HR(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&cv,IID_PPV_ARGS(&dst)));
 D3D12_DESCRIPTOR_HEAP_DESC dh{}; dh.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV; dh.NumDescriptors=2; ComPtr<ID3D12DescriptorHeap> dsv; HR(dev->CreateDescriptorHeap(&dh,IID_PPV_ARGS(&dsv)));
 auto srcDsv=dsv->GetCPUDescriptorHandleForHeapStart();D3D12_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;dv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;dev->CreateDepthStencilView(src.Get(),&dv,srcDsv);
 auto both=D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL;list->ClearDepthStencilView(srcDsv,both,.875f,9,0,nullptr);D3D12_RECT active{0,0,2,4};list->ClearDepthStencilView(srcDsv,both,.375f,73,1,&active);
 dh={};dh.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;dh.NumDescriptors=2;dh.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;ComPtr<ID3D12DescriptorHeap> srv;HR(dev->CreateDescriptorHeap(&dh,IID_PPV_ARGS(&srv)));
 regpu::native_scene::DepthStencilSeed seed;CHECK(seed.Initialize(dev.Get()));std::vector<ComPtr<ID3D12Resource>> retained;auto old=dst;
 CHECK(!seed.CreateAndRecord(dev.Get(),list.Get(),srv.Get(),0,dsv.Get(),1,src.Get(),5,4,4,2,retained,dst));CHECK(dst.Get()==old.Get() && retained.empty());
 CHECK(seed.CreateAndRecord(dev.Get(),list.Get(),srv.Get(),0,dsv.Get(),1,src.Get(),2,4,4,2,retained,dst));CHECK(dst && dst.Get()!=old.Get() && retained.size()==2);
 // Source is restored to DEPTH_WRITE by the helper; the debug layer checks these barriers when available.
 transition(list.Get(),src.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE);transition(list.Get(),src.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);transition(list.Get(),dst.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE);
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[2]{};UINT rows[2]{};UINT64 rowBytes[2]{},size=0;dev->GetCopyableFootprints(&desc,0,2,0,fp,rows,rowBytes,&size);CHECK(size && rows[0]>=2 && rows[1]>=2);
 D3D12_RESOURCE_DESC buf{};buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buf.Width=size;buf.Height=1;buf.DepthOrArraySize=1;buf.MipLevels=1;buf.SampleDesc.Count=1;buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;hp.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> readback;HR(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
 for(UINT i=0;i<2;i++){ D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=dst.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=i;to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp[i];list->CopyTextureRegion(&to,0,0,0,&from,nullptr); }
 HR(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);ComPtr<ID3D12Fence> fence;HR(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CHECK(event);HR(queue->Signal(fence.Get(),1));HR(fence->SetEventOnCompletion(1,event));DWORD wait=WaitForSingleObject(event,10000);CloseHandle(event);CHECK(wait==WAIT_OBJECT_0);
 unsigned char* bytes=nullptr;D3D12_RANGE readRange{0,SIZE_T(size)};HR(readback->Map(0,&readRange,reinterpret_cast<void**>(&bytes)));
 // Depth plane is 32-bit float; stencil plane has the actual footprint format's texel width.
 const UINT depthBytes=UINT(rowBytes[0]/4),stencilBytes=UINT(rowBytes[1]/4);CHECK((depthBytes==4 || depthBytes==8) && (stencilBytes==1 || stencilBytes==8));
 for(UINT y=0;y<2;y++)for(UINT x=0;x<4;x++){float z=0;std::memcpy(&z,bytes+fp[0].Offset+y*fp[0].Footprint.RowPitch+x*depthBytes,4);unsigned char st=bytes[fp[1].Offset+y*fp[1].Footprint.RowPitch+x*stencilBytes+(stencilBytes==8?4:0)];if(z!=.375f || st!=73){std::printf("FAIL pixel %u,%u depth=%g stencil=%u stencilBytes=%u\n",x,y,double(z),unsigned(st),stencilBytes);return 1;}}
 D3D12_RANGE none{0,0};readback->Unmap(0,&none);
 if(info){auto n=info->GetNumStoredMessagesAllowedByRetrievalFilter();for(UINT64 i=0;i<n;i++){SIZE_T z=0;HR(info->GetMessage(i,nullptr,&z));std::vector<char> mem(z);auto m=reinterpret_cast<D3D12_MESSAGE*>(mem.data());HR(info->GetMessage(i,m,&z));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::printf("D3D12 ERROR: %s\n",m->pDescription);return 1;}}}
 std::printf("PASS WARP isolated helper 1x D32S8 nearest active 2x4 -> 4x2, both depth and stencil, debug=%s\n",debugOn?"enabled":"unavailable");return 0;
}
