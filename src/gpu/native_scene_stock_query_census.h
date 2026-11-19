#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <climits>
#include <cstdlib>
#include <cstring>

struct StockQueryCensus {
    static constexpr unsigned kMax=16;
    static constexpr unsigned kNoClearMax=2;
    static bool CommonGrid() {
        static const bool selected=[]{
            const char* e=std::getenv("REGPU_NATIVE_SCENE_TEXTURED_COMMON_GRID");
            return e && !std::strcmp(e,"1");
        }();
        return selected;
    }
    static bool FirstTextured() {
        static const bool selected=[]{
            const char* e=std::getenv("REGPU_NATIVE_SCENE_FIRST_TEXTURED_STOCK_SAMPLES");
            return e && !std::strcmp(e,"1");
        }();
        return selected || CommonGrid();
    }
    static bool DsvDiscovery() {
        static const bool selected=[] {
            const char* e=std::getenv("REGPU_NATIVE_SCENE_STOCK_DSV_DISCOVERY");
            return e && !std::strcmp(e,"1") && !FirstTextured() &&
                !std::getenv("REGPU_NATIVE_SCENE_NO_CLEAR_QUERY_CENSUS") &&
                !std::getenv("REGPU_NATIVE_SCENE_STOCK_QUERY_CENSUS");
        }();
        return selected;
    }
    static bool NoClear() {
        static const bool selected=[]{
            const char* e=std::getenv("REGPU_NATIVE_SCENE_NO_CLEAR_QUERY_CENSUS");
            return e && !std::strcmp(e,"1");
        }();
        return selected || FirstTextured() || DsvDiscovery();
    }
    static bool Enabled() {
        static const bool enabled=[]{
            const char* e=std::getenv("REGPU_NATIVE_SCENE_STOCK_QUERY_CENSUS");
            return e && !std::strcmp(e,"1");
        }();
        return enabled || NoClear();
    }
    static bool InWindow(UINT64 frame) {
        static const UINT64 first=[]() -> UINT64 {
            const char* s=std::getenv("REGPU_NATIVE_SCENE_PRIVATE_PAIR_FRAME");
            if(!s || !*s) return 0;
            UINT64 v=0;
            for(;*s;++s) {
                if(*s<'0' || *s>'9' || v>(UINT64_MAX-UINT(*s-'0'))/10) return 0;
                v=v*10+UINT(*s-'0');
            }
            return v;
        }();
        if(NoClear()) return first==5043 && frame==5043;
        return first && frame>=first && frame-first<8;
    }
    struct Candidate {
        UINT64 epoch=0, draw=0;
        UINT ordinal=0, base=0, colorBase=0, depthBase=0, width=0, height=0;
        ID3D12Resource *color=nullptr, *depth=nullptr;
        bool cleared=false;
    } next;
    struct Disposition {
        UINT64 draw=0, vs=0, ps=0;
        UINT colorBase=0, depthBase=0;
        bool ready=false;
        const char* first=nullptr;
    } dispositions[kNoClearMax]{};
    UINT active=0;
    void Finish(const char* reason) {
        if(!active) return;
        dispositions[active-1].first=reason;
        active=0;
    }
    struct NextDrawScope {
        StockQueryCensus* owner=nullptr;
        UINT ordinal=0;
        ~NextDrawScope() {
            if(owner && ordinal && owner->active==ordinal) owner->Finish("next_draw_not_issued");
        }
    };
    struct Row {
        UINT64 draw=0, vs=0, ps=0;
        UINT ordinal=0, base=0, colorBase=0, depthBase=0, width=0,height=0;
        D3D12_RECT stock{},output{};
    } rows[kMax]{};
    UINT64 frame=0;
    UINT resolves=0, ready=0, clears=0, eligible=0, count=0;
    bool allocationTried=false;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    // Check the successor's paired-readback geometry without creating any target or buffer.
    static bool PairFootprintsFit(ID3D12Device* device,D3D12_RESOURCE_DESC color,
                                  D3D12_RESOURCE_DESC depth,UINT w,UINT h,UINT64 limit) {
        UINT64 end=0;
        for(UINT i=0;i<4;++i) {
            auto desc=(i&1) ? depth : color;
            if(i>=2) { desc.Width=w; desc.Height=h; }
            const UINT n=(i&1) ? 2 : 1;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[2]{};
            UINT rows[2]{}; UINT64 rowBytes[2]{}, total=0;
            device->GetCopyableFootprints(&desc,0,n,(end+511)&~UINT64(511),fp,rows,rowBytes,&total);
            if(!total || total==UINT64_MAX) return false;
            for(UINT j=0;j<n;++j) {
                const UINT pitch=fp[j].Footprint.RowPitch;
                if(!pitch || !rows[j] || rowBytes[j]>pitch || fp[j].Offset>limit ||
                   rows[j]>(limit-fp[j].Offset)/pitch) return false;
                end=fp[j].Offset+UINT64(rows[j])*pitch;
            }
        }
        return true;
    }
    bool Allocate(ID3D12Device* d) {
        if(allocationTried) return heap && buffer;
        allocationTried=true;
        D3D12_QUERY_HEAP_DESC q{}; q.Type=D3D12_QUERY_HEAP_TYPE_OCCLUSION; q.Count=kMax;
        if(FAILED(d->CreateQueryHeap(&q,IID_PPV_ARGS(&heap)))) return false;
        D3D12_HEAP_PROPERTIES props{}; props.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width=kMax*sizeof(UINT64); desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
        desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return SUCCEEDED(d->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer)));
    }
    bool Read(UINT64 fence,UINT64 completed,UINT64 (&samples)[kMax]) const {
        if(!count || count>kMax || !buffer || !fence || completed==UINT64_MAX || completed<fence) return false;
        const D3D12_RANGE range{0,count*sizeof(UINT64)},none{0,0}; void* p=nullptr;
        if(FAILED(buffer->Map(0,&range,&p))) return false;
        if(p) std::memcpy(samples,p,range.End);
        buffer->Unmap(0,&none); return p!=nullptr;
    }
    static void Inc(UINT& v) { if(v<UINT_MAX) ++v; }
};
