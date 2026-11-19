#include "../src/gpu/native_scene_blit.h"
#include "../src/gpu/native_scene_frame_descriptors.h"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace regpu::native_scene;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
#define HR(x) CHECK(SUCCEEDED(x))
static void barrier(ID3D12GraphicsCommandList* l, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
 D3D12_RESOURCE_BARRIER t{}; t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; t.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b}; l->ResourceBarrier(1,&t);
}
int main() {
 ComPtr<ID3D12Debug> debug; bool debugOn=SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))); if(debugOn) debug->EnableDebugLayer();
 ComPtr<IDXGIFactory4> factory; HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory))); ComPtr<IDXGIAdapter> warp; HR(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
 ComPtr<ID3D12Device> dev; HR(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev))); ComPtr<ID3D12InfoQueue> info; if(debugOn) HR(dev.As(&info));
 ColorBlit blit; CHECK(blit.Initialize(dev.Get()));
 D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT; ComPtr<ID3D12CommandQueue> queue; HR(dev->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
 ComPtr<ID3D12CommandAllocator> alloc; HR(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
 ComPtr<ID3D12GraphicsCommandList> list; HR(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list)));
 FrameBlitDescriptors slots; CHECK(slots.Initialize(dev.Get())); CHECK(slots.Begin(0));
 D3D12_HEAP_PROPERTIES hp{}; hp.Type=D3D12_HEAP_TYPE_DEFAULT; D3D12_RESOURCE_DESC tex{}; tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; tex.Width=4; tex.Height=4; tex.DepthOrArraySize=1; tex.MipLevels=1; tex.Format=DXGI_FORMAT_R8G8B8A8_UNORM; tex.SampleDesc.Count=1;
 ComPtr<ID3D12Resource> src,dst; HR(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&tex,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&src))); CHECK(ColorBlit::CreateTarget(dev.Get(),4,2,tex.Format,dst));
 D3D12_RESOURCE_DESC buf{}; buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buf.Width=1024; buf.Height=1; buf.DepthOrArraySize=1; buf.MipLevels=1; buf.SampleDesc.Count=1; buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 hp.Type=D3D12_HEAP_TYPE_UPLOAD; ComPtr<ID3D12Resource> upload; HR(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
 hp.Type=D3D12_HEAP_TYPE_READBACK; ComPtr<ID3D12Resource> readback; HR(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
 unsigned char* bytes=nullptr; HR(upload->Map(0,nullptr,reinterpret_cast<void**>(&bytes))); for(int y=0;y<4;y++) for(int x=0;x<4;x++) { auto p=bytes+y*256+x*4; p[0]=x<2?static_cast<unsigned char>(x*100):255; p[1]=x<2?static_cast<unsigned char>(y*40):255; p[2]=x<2?20:255; p[3]=x<2?static_cast<unsigned char>(20+x*80+y*20):255; } upload->Unmap(0,nullptr);
 D3D12_TEXTURE_COPY_LOCATION from{},to{}; from.pResource=upload.Get(); from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; from.PlacedFootprint.Footprint={tex.Format,4,4,1,256}; to.pResource=src.Get(); to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; list->CopyTextureRegion(&to,0,0,0,&from,nullptr); barrier(list.Get(),src.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
 const auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; BlitRequest req{src.Get(),dst.Get(),2,4,4,2,D3D12_RESOURCE_STATE_COPY_SOURCE,read,true}; std::vector<ComPtr<ID3D12Resource>> retained;
 auto invalid=req; invalid.destination=src.Get(); CHECK(!blit.Record(dev.Get(),list.Get(),slots.srv(),0,slots.rtv(),0,invalid,retained)); invalid=req; invalid.nonAliasing=false; CHECK(!blit.Record(dev.Get(),list.Get(),slots.srv(),0,slots.rtv(),0,invalid,retained)); invalid=req; invalid.sourceWidth=5; CHECK(!blit.Record(dev.Get(),list.Get(),slots.srv(),0,slots.rtv(),0,invalid,retained)); CHECK(retained.empty());
 auto sentinel=dst; auto staged=req; staged.destination=nullptr; invalid=staged; invalid.sourceWidth=5;
 CHECK(!blit.CreateAndRecord(dev.Get(),list.Get(),slots.srv(),0,slots.rtv(),0,invalid,retained,dst)); CHECK(dst.Get()==sentinel.Get()); CHECK(retained.empty());
 CHECK(blit.CreateAndRecord(dev.Get(),list.Get(),slots.srv(),0,slots.rtv(),0,staged,retained,dst)); CHECK(dst && dst.Get()!=sentinel.Get()); CHECK(retained.size()==2);
 // These barriers describe the original states after Record. The debug layer
 // detects a failure to restore them before the subsequent copy operations.
 barrier(list.Get(),src.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST); barrier(list.Get(),src.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE); barrier(list.Get(),dst.Get(),read,D3D12_RESOURCE_STATE_COPY_SOURCE);
 from.pResource=dst.Get(); from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; to.pResource=readback.Get(); to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint.Footprint={tex.Format,4,2,1,256}; list->CopyTextureRegion(&to,0,0,0,&from,nullptr); HR(list->Close()); ID3D12CommandList* lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
 ComPtr<ID3D12Fence> fence; HR(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))); HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CHECK(event); HR(queue->Signal(fence.Get(),1)); CHECK(slots.Submitted(1)); CHECK(!slots.Begin(0)); HR(fence->SetEventOnCompletion(1,event)); DWORD wait=WaitForSingleObject(event,10000); CloseHandle(event); CHECK(wait==WAIT_OBJECT_0);
 D3D12_RANGE range{0,512}; HR(readback->Map(0,&range,reinterpret_cast<void**>(&bytes))); for(int y=0;y<2;y++) for(int x=0;x<4;x++) { float fx=(std::max)(0.f,(std::min)(1.f,(x+.5f)*.5f-.5f)); float fy=(y+.5f)*2.f-.5f; float expected[4]={fx*100,fy*40,20,20+fx*80+fy*20}; for(int c=0;c<4;c++){ int actual=bytes[y*256+x*4+c]; if(std::abs(actual-expected[c])>2){ std::printf("pixel %d,%d channel %d actual=%d expected=%.2f\n",x,y,c,actual,expected[c]); return 1; } } } D3D12_RANGE none{0,0}; readback->Unmap(0,&none);
 // Reuse the same descriptor slots only after the prior fence completed.
 CHECK(slots.Begin(fence->GetCompletedValue())); retained.clear(); HR(alloc->Reset()); HR(list->Reset(alloc.Get(),nullptr));
 auto first=dst; CHECK(blit.CreateAndRecord(dev.Get(),list.Get(),slots.srv(),0,slots.rtv(),0,staged,retained,dst));
 CHECK(dst && dst.Get()!=first.Get() && retained.size()==2); HR(list->Close());
 lists[0]=list.Get(); queue->ExecuteCommandLists(1,lists); HR(queue->Signal(fence.Get(),2)); CHECK(slots.Submitted(2)); CHECK(!slots.Begin(1));
 event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CHECK(event); HR(fence->SetEventOnCompletion(2,event));
 wait=WaitForSingleObject(event,10000); CloseHandle(event); CHECK(wait==WAIT_OBJECT_0);
 if(info) { auto n=info->GetNumStoredMessagesAllowedByRetrievalFilter(); for(UINT64 i=0;i<n;i++){ SIZE_T sz=0; HR(info->GetMessage(i,nullptr,&sz)); std::vector<char> mem(sz); auto m=reinterpret_cast<D3D12_MESSAGE*>(mem.data()); HR(info->GetMessage(i,m,&sz)); if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){ std::printf("D3D12 ERROR: %s\n",m->pDescription); return 1; } } }
 std::printf("PASS WARP checked create/publish, padded 2x4 in 4x4 -> 4x2 mixed-axis RGBA, failed output unchanged, restored states; debug=%s\n",debugOn?"enabled":"unavailable"); return 0;
}
