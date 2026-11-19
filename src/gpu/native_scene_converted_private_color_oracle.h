#pragma once
#include "gpu/native_scene_converted_private_color.h"
#include "gpu/native_scene_private_pair_readback.h"

struct ConvertedPrivateColorOracle {
    using Resource=ConvertedPrivateColor::Resource;
    Resource seed,readback;
    static constexpr UINT width=1920,height=1080,pitch=width*8;
    static constexpr UINT64 slice=UINT64(pitch)*height;
    static_assert(pitch%D3D12_TEXTURE_DATA_PITCH_ALIGNMENT==0 && slice%D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT==0);
    bool recorded=false;

    bool Prepare(ID3D12Device* device,const ConvertedPrivateColor& color) {
        if (!device || !color.image || color.roi.left || color.roi.top || color.roi.right!=width ||
            color.roi.bottom!=height || seed || readback) return false;
        auto desc=color.image->GetDesc();
        desc.Width=width; desc.Height=height; desc.Flags=D3D12_RESOURCE_FLAG_NONE;
        D3D12_RESOURCE_DESC buf{};
        buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buf.Width=3*slice;
        buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1;
        buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        const auto liveSize=device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;
        const auto readSize=device->GetResourceAllocationInfo(0,1,&buf).SizeInBytes;
        constexpr UINT64 available=PrivatePairReadback::kMaxBytes-ConvertedPrivateColor::limit;
        if (!liveSize || !readSize || liveSize>available || readSize>available-liveSize) return false;
        Resource saved,output;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
                D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&saved)))) return false;
        heap.Type=D3D12_HEAP_TYPE_READBACK;
        if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buf,
                D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&output))) || FAILED(device->GetDeviceRemovedReason())) return false;
        seed=std::move(saved); readback=std::move(output); return true;
    }

    template<class Commands,class Candidate,class Stock>
    void Record(Commands& cmd,const ConvertedPrivateColor& color,Candidate&& candidate,Stock&& stock) {
        auto* live=color.stock.Get();
        auto barrier=[&](ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
            D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};
            cmd.ResourceBarrier(1,&b);
        };
        auto location=[](ID3D12Resource* resource) {
            D3D12_TEXTURE_COPY_LOCATION l{}; l.pResource=resource; l.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; return l;
        };
        auto real=location(live),saved=location(seed.Get());
        auto snapshot=[&](const D3D12_TEXTURE_COPY_LOCATION& from,UINT index) {
            D3D12_TEXTURE_COPY_LOCATION to{}; to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.pResource=readback.Get();
            to.PlacedFootprint.Offset=index*slice;
            to.PlacedFootprint.Footprint={DXGI_FORMAT_R16G16B16A16_FLOAT,width,height,1,pitch};
            cmd.CopyTextureRegion(&to,0,0,0,&from,&color.roi);
        };
        auto unbind=[&]{cmd.OMSetRenderTargets(0,nullptr,FALSE,nullptr);};
        auto bind=[&]{cmd.OMSetRenderTargets(1,&color.stockView,FALSE,&color.depthView);};
        constexpr auto rt=D3D12_RESOURCE_STATE_RENDER_TARGET,src=D3D12_RESOURCE_STATE_COPY_SOURCE,dst=D3D12_RESOURCE_STATE_COPY_DEST;
        unbind(); barrier(live,rt,src);
        cmd.CopyTextureRegion(&saved,0,0,0,&real,&color.roi);
        barrier(seed.Get(),dst,src); snapshot(saved,0);
        barrier(live,src,rt); bind();
        candidate();
        unbind(); barrier(live,rt,src); snapshot(real,1);
        barrier(live,src,dst);
        cmd.CopyTextureRegion(&real,0,0,0,&saved,&color.roi);
        barrier(live,dst,rt); bind();
        stock();
        unbind(); barrier(live,rt,src); snapshot(real,2);
        barrier(live,src,rt); bind(); recorded=true;
    }

    struct Result { bool readable=false; UINT64 pixels=0,different=0,stockChanged=0; };
    static Result Compare(const unsigned char* pre,const unsigned char* candidate,const unsigned char* stock,
                          UINT rowPitch,D3D12_RECT region) {
        if (region.left || region.top) return {};
        const auto match=PrivateDrawColorBefore::CompareRows(candidate,rowPitch,stock,rowPitch,region);
        const auto delta=PrivateDrawColorBefore::CompareRows(pre,rowPitch,stock,rowPitch,region);
        if (!match.readable || !delta.readable || match.pixels!=delta.pixels) return {};
        return {true,match.pixels,match.changed,delta.changed};
    }
    Result Finish() const {
        if (!recorded || !readback) return {};
        D3D12_RANGE range{0,SIZE_T(3*slice)},none{0,0};
        unsigned char* p=nullptr;
        if (FAILED(readback->Map(0,&range,reinterpret_cast<void**>(&p)))) return {};
        const Result result=p ? Compare(p,p+slice,p+2*slice,pitch,{0,0,LONG(width),LONG(height)}) : Result{};
        readback->Unmap(0,&none);
        return result;
    }
};
