#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

struct ConvertedPrivateColor {
    struct HeapReceipt {
        // Non-owning identities. The enclosing frame or retired holder retains the heap.
        ID3D12DescriptorHeap* heap=nullptr;
        ID3D12Device* device=nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE view{};
        D3D12_DESCRIPTOR_HEAP_DESC desc{};

        bool Matches(const HeapReceipt& current) const {
            return heap && heap==current.heap && device && device==current.device &&
                view.ptr && view.ptr==current.view.ptr && desc.Type==current.desc.Type &&
                desc.NumDescriptors==current.desc.NumDescriptors && desc.Flags==current.desc.Flags &&
                desc.NodeMask==current.desc.NodeMask;
        }
        bool SingleCpuRtv(UINT nodes) const {
            return nodes && heap && device && view.ptr && desc.Type==D3D12_DESCRIPTOR_HEAP_TYPE_RTV &&
                desc.NumDescriptors==1 && desc.Flags==D3D12_DESCRIPTOR_HEAP_FLAG_NONE &&
                (desc.NodeMask==0 || (nodes==1 && desc.NodeMask==1));
        }
    } heapReceipt;
    using Resource=Microsoft::WRL::ComPtr<ID3D12Resource>;
    Resource stock,depth,image;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv,stockRtvHeap,stockDsvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE stockView{},depthView{};
    D3D12_BOX roi{};
    bool borrowed=false;
    static constexpr UINT64 limit=64ull<<20;

    static bool AcceptState(UINT state) {
        // postState is present only for a 1-RT, 1-sample, D32S8 PSO. Require all four color channels,
        // depth writes disabled, stencil disabled, and no writable DSV plane.
        return (state&0x80000000u) && ((state>>20)&15)==15 && !(state&(0x100u|8u|3u));
    }

    static bool Fits(const D3D12_RESOURCE_DESC& c,const D3D12_RESOURCE_DESC& z,const D3D12_RECT& rect) {
        return c.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && z.Dimension==c.Dimension &&
            c.Format==DXGI_FORMAT_R16G16B16A16_FLOAT && z.Format==DXGI_FORMAT_R32G8X24_TYPELESS &&
            c.Flags==D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET && (z.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) &&
            c.DepthOrArraySize==1 && z.DepthOrArraySize==1 && c.MipLevels==1 && z.MipLevels==1 &&
            c.SampleDesc.Count==1 && z.SampleDesc.Count==1 && !c.SampleDesc.Quality && !z.SampleDesc.Quality &&
            c.Layout==D3D12_TEXTURE_LAYOUT_UNKNOWN && c.Width==z.Width && c.Height==z.Height &&
            rect.left==0 && rect.top==0 && rect.right==1920 && rect.bottom==1080 &&
            c.Width>=UINT(rect.right) && c.Height>=UINT(rect.bottom);
    }

    const char* Prepare(ID3D12Device* device,ID3D12Resource* color,ID3D12Resource* z,ID3D12DescriptorHeap* liveRtvHeap,
                 ID3D12DescriptorHeap* liveDsvHeap,D3D12_CPU_DESCRIPTOR_HANDLE liveRtv,D3D12_CPU_DESCRIPTOR_HANDLE liveDsv,
                 const D3D12_RECT& rect,bool borrow,bool recurring,ID3D12DescriptorHeap* retiredView,
                 const HeapReceipt* prior) {
        if (!device || !color || !z || color==z || !liveRtvHeap || !liveDsvHeap || !liveRtv.ptr || !liveDsv.ptr) return "attachment_input";
        const auto desc=color->GetDesc();
        if (!Fits(desc,z->GetDesc(),rect)) return "attachment_layout";
        if (!borrow) {
            const auto allocation=device->GetResourceAllocationInfo(0,1,&desc);
            if (!allocation.SizeInBytes || allocation.SizeInBytes>limit) return "color_size";
            D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,
                                                       nullptr,IID_PPV_ARGS(&image)))) return "color_allocation";
        }
        D3D12_DESCRIPTOR_HEAP_DESC views{};
        views.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; views.NumDescriptors=1;
        if (prior || retiredView) {
            if (!borrow || !recurring || !prior || !retiredView || retiredView!=prior->heap)
                return "retired_heap_identity";
            rtv=retiredView;
        } else if (FAILED(device->CreateDescriptorHeap(&views,IID_PPV_ARGS(&rtv)))) return "rtv_allocation";
        if (recurring) {
            Microsoft::WRL::ComPtr<ID3D12Device> owner;
            if (FAILED(rtv->GetDevice(IID_PPV_ARGS(&owner))) || owner.Get()!=device) return "rtv_device";
            heapReceipt={rtv.Get(),owner.Get(),rtv->GetCPUDescriptorHandleForHeapStart(),rtv->GetDesc()};
            if (!heapReceipt.SingleCpuRtv(device->GetNodeCount())) return "rtv_heap_shape";
            if (prior && !prior->Matches(heapReceipt)) return "retired_heap_descriptor";
        }
        if (borrow) {
            image=color;
            borrowed=true;
        } else {
            device->CreateRenderTargetView(image.Get(),nullptr,rtv->GetCPUDescriptorHandleForHeapStart());
        }
        if (FAILED(device->GetDeviceRemovedReason())) return "device_removed";
        stock=color; depth=z; stockRtvHeap=liveRtvHeap; stockDsvHeap=liveDsvHeap;
        stockView=liveRtv; depthView=liveDsv;
        roi={UINT(rect.left),UINT(rect.top),0,UINT(rect.right),UINT(rect.bottom),1};
        return nullptr;
    }

    // Match GetSurface's ordered default-view creation. The original descriptor
    // may still be awaiting playback when the private heap is allocated.
    template<typename Commands,typename Issue>
    static void RecordBorrowed(Commands& cmd,ID3D12Device* device,ID3D12Resource* color,
                               D3D12_CPU_DESCRIPTOR_HANDLE stockView,D3D12_CPU_DESCRIPTOR_HANDLE privateView,
                               D3D12_CPU_DESCRIPTOR_HANDLE dsv,Issue&& issue) {
        cmd.CreateRenderTargetView(device,color,privateView);
        cmd.OMSetRenderTargets(1,&privateView,FALSE,&dsv);
        issue();
        cmd.OMSetRenderTargets(1,&stockView,FALSE,&dsv);
    }

    template<typename Commands,typename Issue>
    static void Record(Commands& cmd,ID3D12Resource* live,ID3D12Resource* temporary,
                       D3D12_CPU_DESCRIPTOR_HANDLE liveView,D3D12_CPU_DESCRIPTOR_HANDLE privateView,
                       D3D12_CPU_DESCRIPTOR_HANDLE dsv,const D3D12_BOX& box,Issue&& issue) {
        auto transition=[&](ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
            D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};
            cmd.ResourceBarrier(1,&b);
        };
        auto copy=[&](ID3D12Resource* dst,ID3D12Resource* src) {
            D3D12_TEXTURE_COPY_LOCATION to{},from{};
            to.Type=from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.pResource=dst; from.pResource=src;
            cmd.CopyTextureRegion(&to,box.left,box.top,0,&from,&box);
        };
        cmd.OMSetRenderTargets(0,nullptr,FALSE,nullptr);
        transition(live,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
        copy(temporary,live);
        transition(temporary,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd.OMSetRenderTargets(1,&privateView,FALSE,&dsv);
        issue();
        cmd.OMSetRenderTargets(0,nullptr,FALSE,nullptr);
        transition(temporary,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(live,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        copy(live,temporary);
        transition(live,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd.OMSetRenderTargets(1,&liveView,FALSE,&dsv);
    }
};
