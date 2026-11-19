#pragma once
#include "gpu/recorded_list.h"
#include "gpu/native_scene_converted_output.h"
#include "gpu/native_scene_private_samples.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

// A single exact grid. Full depth/stencil source subresources are staged separately,
// then re-described as full logical destination planes with the same padded row pitch.
struct TexturedCommonGrid : ImmutableDrawSamplers {
    static constexpr UINT width=2501, height=751;
    static constexpr UINT64 limit=256ull<<20;
    using Resource=Microsoft::WRL::ComPtr<ID3D12Resource>;
    Resource primary, stock[2], clone[2], stage, readback;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv,dsv,srvHeap;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
    PrivatePassedSamples samples;
    UINT descriptor=0;
    UINT64 frame=0,draw=0,serial=0,epoch=0,table=0,fence=0,stride=0,stageBytes=0;
    bool recorded=false,signalFailed=false;
    struct Plane {
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT full{},logicalFootprint{};
        UINT image=0, sub=0, step=0, skip=0, meaning=0;
        UINT64 roi=0;
    } plane[3];
    struct Counts { UINT64 seed[3]{}, mismatch[3]{}, stockChanged[3]{}, passed[2]{}; bool readable=false; };
    static UINT64 Align(UINT64 n) { return (n+511)&~UINT64(511); }
    static constexpr D3D12_RESOURCE_STATES Usage(UINT i) {
        return i ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    static void Barrier(RecordedList& list, ID3D12Resource* r,
                        D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list.ResourceBarrier(1,&b);
    }
    static D3D12_TEXTURE_COPY_LOCATION Tex(ID3D12Resource* r,UINT sub) {
        D3D12_TEXTURE_COPY_LOCATION l{};l.pResource=r;l.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;l.SubresourceIndex=sub;return l;
    }
    static D3D12_TEXTURE_COPY_LOCATION Buf(ID3D12Resource* r,D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp) {
        D3D12_TEXTURE_COPY_LOCATION l{};l.pResource=r;l.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;l.PlacedFootprint=fp;return l;
    }
    static D3D12_RESOURCE_DESC Buffer(UINT64 bytes) {
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;
        d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;return d;
    }
    static bool Create(ID3D12Device* device,const D3D12_RESOURCE_DESC& desc,D3D12_HEAP_TYPE type,
                       D3D12_RESOURCE_STATES initial,Resource& out) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;
        return SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,initial,nullptr,IID_PPV_ARGS(&out)));
    }
    bool Prepare(ID3D12Device* d, ID3D12Resource* color,ID3D12Resource* depth) {
        if(!d || !primary || !color || !depth || color==depth || color==primary.Get() || depth==primary.Get()) return false;
        D3D12_RESOURCE_DESC live[]={color->GetDesc(),depth->GetDesc()}, scaled[2]={live[0],live[1]};
        if(live[0].Format!=DXGI_FORMAT_R16G16B16A16_FLOAT || live[1].Format!=DXGI_FORMAT_R32G8X24_TYPELESS ||
           !(live[0].Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) || !(live[1].Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) return false;
        for(UINT i=0;i<2;++i) {
            if(live[i].Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || live[i].Width<width || live[i].Height<height ||
               live[i].MipLevels!=1 || live[i].DepthOrArraySize!=1 || live[i].SampleDesc.Count!=1 ||
               live[i].SampleDesc.Quality!=0) return false;
            scaled[i].Width=width;scaled[i].Height=height;
        }
        for(UINT i=0;i<3;++i) {
            auto& p=plane[i];p.image=i!=0;p.sub=i==2;
            UINT rows=0,smallRows=0;UINT64 bytes=0,row=0,smallRow=0;
            d->GetCopyableFootprints(&live[p.image],p.sub,1,Align(stageBytes),&p.full,&rows,&row,&bytes);
            if(!bytes || bytes==UINT64_MAX || !p.full.Footprint.RowPitch || p.full.Offset>limit ||
               rows!=(UINT)live[p.image].Height || rows>(limit-p.full.Offset)/p.full.Footprint.RowPitch) return false;
            stageBytes=p.full.Offset+UINT64(rows)*p.full.Footprint.RowPitch;
            d->GetCopyableFootprints(&scaled[p.image],p.sub,1,0,&p.logicalFootprint,&smallRows,&smallRow,&bytes);
            if(!bytes || bytes==UINT64_MAX || smallRows!=height || p.logicalFootprint.Footprint.Height!=height ||
               p.logicalFootprint.Footprint.Width!=width || p.logicalFootprint.Footprint.Depth!=1 ||
               p.full.Footprint.Format!=p.logicalFootprint.Footprint.Format || smallRow%width || !live[p.image].Width ||
               row%live[p.image].Width || row/live[p.image].Width!=smallRow/width ||
               smallRow>p.full.Footprint.RowPitch) return false;
            p.step=UINT(smallRow/width);
            if((i==0 && p.step!=8)||(i==1 && p.step!=4 && p.step!=8)||(i==2 && p.step!=1 && p.step!=8)) return false;
            p.skip=(i==2 && p.step==8)?4:0;p.meaning=i==0?8:i==1?4:1;
            p.logicalFootprint.Offset=p.full.Offset;p.logicalFootprint.Footprint.RowPitch=p.full.Footprint.RowPitch;
            p.roi=Align(stride);stride=p.roi+UINT64(height)*p.logicalFootprint.Footprint.RowPitch;
        }
        stride=Align(stride);
        if(!stride || stride>limit/4) return false;
        const auto scratch=Buffer(stageBytes),read=Buffer(4*stride),query=Buffer(PrivatePassedSamples::kBytes);
        UINT64 used=0;
        for(const auto& desc:{scaled[0],scaled[1],scratch,read,query}) {
            const auto info=d->GetResourceAllocationInfo(0,1,&desc);
            if(!info.SizeInBytes || info.SizeInBytes>limit-used) return false;
            used+=info.SizeInBytes;
        }
        if(!Create(d,scaled[0],D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,clone[0]) ||
           !Create(d,scaled[1],D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,clone[1]) ||
           !Create(d,scratch,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,stage) ||
           !Create(d,read,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,readback)) return false;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=1;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if(FAILED(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv)))) return false;
        hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        if(FAILED(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsv)))) return false;
        d->CreateRenderTargetView(clone[0].Get(),nullptr,rtv->GetCPUDescriptorHandleForHeapStart());
        D3D12_DEPTH_STENCIL_VIEW_DESC view{};view.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
        d->CreateDepthStencilView(clone[1].Get(),&view,dsv->GetCPUDescriptorHandleForHeapStart());
        samples.requested=true;if(!samples.Allocate(d)) return false;
        stock[0]=color;stock[1]=depth;
        return SUCCEEDED(d->GetDeviceRemovedReason());
    }
    // Arms 0/2 hold stock before/after, 1/3 hold private before/after.
    void CaptureStock(RecordedList& cmd,UINT arm) {
        if(arm) Barrier(cmd,stage.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        for(UINT i=0;i<2;++i) Barrier(cmd,stock[i].Get(),Usage(i),D3D12_RESOURCE_STATE_COPY_SOURCE);
        for(const auto& p:plane) {
            auto src=Tex(stock[p.image].Get(),p.sub),dst=Buf(stage.Get(),p.full);
            cmd.CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        }
        for(UINT i=0;i<2;++i) Barrier(cmd,stock[i].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,Usage(i));
        Barrier(cmd,stage.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
        for(const auto& p:plane)
            cmd.CopyBufferRegion(readback.Get(),arm*stride+p.roi,stage.Get(),p.full.Offset,UINT64(height)*p.full.Footprint.RowPitch);
    }
    void Seed(RecordedList& cmd) {
        CaptureStock(cmd,0);
        for(const auto& p:plane) {
            auto src=Buf(stage.Get(),p.logicalFootprint),dst=Tex(clone[p.image].Get(),p.sub);
            cmd.CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        }
        for(UINT i=0;i<2;++i) Barrier(cmd,clone[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,Usage(i));
        CapturePrivate(cmd,1);
    }
    void CapturePrivate(RecordedList& cmd,UINT arm) {
        for(UINT i=0;i<2;++i) Barrier(cmd,clone[i].Get(),Usage(i),D3D12_RESOURCE_STATE_COPY_SOURCE);
        for(const auto& p:plane) {
            auto fp=p.logicalFootprint;fp.Offset=arm*stride+p.roi;
            auto src=Tex(clone[p.image].Get(),p.sub),dst=Buf(readback.Get(),fp);
            cmd.CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        }
        for(UINT i=0;i<2;++i) Barrier(cmd,clone[i].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,Usage(i));
    }
    Counts Read(UINT64 completed) const {
        Counts out;if(!recorded || signalFailed || !fence || completed==UINT64_MAX || completed<fence ||
                      !samples.Read(fence,completed,out.passed)) return out;
        const D3D12_RANGE range{0,SIZE_T(4*stride)},none{0,0};void* data=nullptr;
        if(FAILED(readback->Map(0,&range,&data))) return out;
        if(data) for(UINT i=0;i<3;++i) {
            const auto& p=plane[i];
            for(UINT y=0;y<height;++y) {
                const auto* row=static_cast<const unsigned char*>(data)+p.roi+UINT64(y)*p.logicalFootprint.Footprint.RowPitch+p.skip;
                for(UINT x=0;x<width;++x) {
                    const auto* b=row+UINT64(x)*p.step;
                    out.seed[i]+=std::memcmp(b,b+stride,p.meaning)!=0;
                    out.mismatch[i]+=std::memcmp(b+2*stride,b+3*stride,p.meaning)!=0;
                    out.stockChanged[i]+=std::memcmp(b,b+2*stride,p.meaning)!=0;
                }
            }
        }
        readback->Unmap(0,&none);out.readable=data!=nullptr;return out;
    }
};
