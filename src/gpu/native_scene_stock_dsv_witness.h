#pragma once
#include "gpu/native_scene_textured_common_grid.h"
#include <atomic>

// Scoped to the second frame-5043 resolve and its immediate master-list draw.
struct StockDsvWitness {
    using Grid = TexturedCommonGrid;
    static constexpr UINT64 vs = 0xB0114762236B889Full, ps = 0x8BE0D4632B7D3122ull;
    static constexpr UINT64 cookie[2] = {0x1739E7C5A82BF461ull, 0xC79D21B64FA0538Eull};
    static constexpr GUID tag = {0x41e09a47,0xe5a2,0x4944,{0x87,0xac,0x18,0xdf,0xb3,0x02,0x2a,0xe3}};
    static UINT WritePlanes(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd, bool colorSilent=true) {
        if (pd.DSVFormat != DXGI_FORMAT_D32_FLOAT_S8X24_UINT || pd.NumRenderTargets != 1 ||
            pd.SampleDesc.Count != 1 || (colorSilent && pd.BlendState.RenderTarget[0].RenderTargetWriteMask)) return 0;
        const auto& ds = pd.DepthStencilState;
        auto writes = [&](const D3D12_DEPTH_STENCILOP_DESC& f) {
            const bool stencilPass=f.StencilFunc!=D3D12_COMPARISON_FUNC_NEVER;
            const bool depthPass=!ds.DepthEnable || ds.DepthFunc!=D3D12_COMPARISON_FUNC_NEVER;
            const bool depthFail=ds.DepthEnable && ds.DepthFunc!=D3D12_COMPARISON_FUNC_ALWAYS;
            return (f.StencilFunc!=D3D12_COMPARISON_FUNC_ALWAYS && f.StencilFailOp!=D3D12_STENCIL_OP_KEEP) ||
                   (stencilPass && depthFail && f.StencilDepthFailOp!=D3D12_STENCIL_OP_KEEP) ||
                   (stencilPass && depthPass && f.StencilPassOp!=D3D12_STENCIL_OP_KEEP);
        };
        const auto cull = pd.RasterizerState.CullMode;
        const bool stencil = ds.StencilEnable && ds.StencilWriteMask &&
            ((cull != D3D12_CULL_MODE_FRONT && writes(ds.FrontFace)) ||
             (cull != D3D12_CULL_MODE_BACK && writes(ds.BackFace)));
        return (ds.DepthEnable && ds.DepthFunc!=D3D12_COMPARISON_FUNC_NEVER && ds.DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ALL ? 1u : 0u) | (stencil ? 2u : 0u);
    }
    static void Tag(ID3D12PipelineState* pso, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd) {
        static std::atomic<unsigned> used{0};
        const UINT mask = WritePlanes(pd);
        if (!mask || !pso) return;
        unsigned prior = used.load();
        while (prior < 8 && !used.compare_exchange_weak(prior, prior + 1)) {}
        if (prior < 8) pso->SetPrivateData(tag, sizeof(mask), &mask);
    }

    Grid::Resource depth, readback, stamps;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT plane[2]{};
    UINT step[2]{}, planes=0;
    D3D12_RECT roi{};
    UINT64 stride=0, size=0, frame=0, draw=0, fence=0;
    bool recorded=false, failed=false;

    bool Prepare(ID3D12Device* device, ID3D12Resource* source, ID3D12PipelineState* pso, D3D12_RECT rect) {
        UINT bytes = sizeof(planes);
        if (!source || !pso || FAILED(pso->GetPrivateData(tag, &bytes, &planes)) ||
            bytes != sizeof(planes) || !planes || planes > 3) return false;
        const auto desc = source->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.MipLevels != 1 ||
            desc.DepthOrArraySize != 1 || desc.Format != DXGI_FORMAT_R32G8X24_TYPELESS ||
            desc.SampleDesc.Count != 1 || !(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) ||
            rect.left < 0 || rect.top < 0 || rect.right <= rect.left || rect.bottom <= rect.top ||
            UINT64(rect.right) > desc.Width || UINT(rect.bottom) > desc.Height) return false;
        for (UINT i=0; i<2; ++i) {
            UINT rows=0;
            UINT64 row=0, total=0;
            auto& fp = plane[i];
            device->GetCopyableFootprints(&desc, i, 1, Grid::Align(stride), &fp, &rows, &row, &total);
            if (!total || total == UINT64_MAX || rows != desc.Height || !fp.Footprint.RowPitch ||
                fp.Footprint.Width != desc.Width || fp.Footprint.Height != rows || fp.Footprint.Depth != 1 ||
                fp.Offset >= Grid::limit || rows > (Grid::limit-fp.Offset)/fp.Footprint.RowPitch ||
                row % desc.Width || row > fp.Footprint.RowPitch) return false;
            step[i]=UINT(row/desc.Width);
            if (step[i] != 8 && step[i] != (i ? 1u : 4u)) return false;
            const auto format=fp.Footprint.Format;
            const bool packed=format==DXGI_FORMAT_R32G8X24_TYPELESS ||
                format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT || format==(i ? DXGI_FORMAT_X32_TYPELESS_G8X24_UINT : DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS);
            const bool compact=i ? (format==DXGI_FORMAT_R8_UINT || format==DXGI_FORMAT_R8_TYPELESS)
                                 : (format==DXGI_FORMAT_R32_FLOAT || format==DXGI_FORMAT_R32_TYPELESS || format==DXGI_FORMAT_R32_UINT);
            if (!(step[i]==8 ? packed : compact)) return false;
            stride=fp.Offset + UINT64(rows)*fp.Footprint.RowPitch;
        }
        stride=Grid::Align(stride);
        if (!stride || stride > (Grid::limit-sizeof(cookie))/2) return false;
        size=2*stride+sizeof(cookie);
        auto read=Grid::Buffer(size), upload=Grid::Buffer(sizeof(cookie));
        auto a=device->GetResourceAllocationInfo(0,1,&read), b=device->GetResourceAllocationInfo(0,1,&upload);
        if (!a.SizeInBytes || a.SizeInBytes>Grid::limit || !b.SizeInBytes || b.SizeInBytes>Grid::limit-a.SizeInBytes ||
            !Grid::Create(device, read, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, readback) ||
            !Grid::Create(device, upload, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, stamps)) return false;
        const D3D12_RANGE none{0,0}, markerRange{SIZE_T(2*stride),SIZE_T(size)}, written{0,sizeof(cookie)};
        void* ptr=nullptr;
        if (FAILED(readback->Map(0,&none,&ptr))) return false;
        std::memset(static_cast<unsigned char*>(ptr)+2*stride,0,sizeof(cookie));
        readback->Unmap(0,&markerRange);
        if (FAILED(stamps->Map(0,&none,&ptr))) return false;
        std::memcpy(ptr,cookie,sizeof(cookie));
        stamps->Unmap(0,&written);
        depth=source;
        roi=rect;
        return true;
    }
    void Snapshot(RecordedList& cmd, UINT arm) {
        Grid::Barrier(cmd,depth.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        for (UINT i=0;i<2;++i) {
            auto fp=plane[i];
            fp.Offset+=arm*stride;
            const auto dst=Grid::Buf(readback.Get(),fp), src=Grid::Tex(depth.Get(),i);
            cmd.CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        }
        Grid::Barrier(cmd,depth.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmd.CopyBufferRegion(readback.Get(),2*stride+arm*8,stamps.Get(),arm*8,8);
    }
    static bool Changed(const unsigned char* a,const unsigned char* b,UINT plane,UINT step) {
        const UINT skip=plane && step==8 ? 4 : 0;
        return std::memcmp(a+skip,b+skip,plane ? 1 : 4)!=0;
    }
    static bool Accepted(UINT mask,UINT64 z,UINT64 s) {
        return (z || s) && (!z || (mask&1)) && (!s || (mask&2));
    }
    struct FirstChange { LONG x=-1,y=-1; UINT before=0,after=0; };
    bool Read(UINT64 completed,UINT64 (&delta)[2],FirstChange (&first)[2]) const {
        if (!recorded || failed || !fence || completed==UINT64_MAX || completed<fence) return false;
        const D3D12_RANGE range{0,SIZE_T(size)}, none{0,0};
        void* ptr=nullptr;
        if (FAILED(readback->Map(0,&range,&ptr))) return false;
        const auto* data=static_cast<const unsigned char*>(ptr);
        const bool executed=std::memcmp(data+2*stride,cookie,sizeof(cookie))==0;
        if (executed) for (UINT i=0;i<2;++i) for (LONG y=roi.top;y<roi.bottom;++y) {
            const auto* row=data+plane[i].Offset+UINT64(y)*plane[i].Footprint.RowPitch;
            for (LONG x=roi.left;x<roi.right;++x) {
                const auto* p=row+UINT64(x)*step[i];
                if (Changed(p,p+stride,i,step[i])) {
                    if (!delta[i]) {
                        auto& f=first[i];
                        f.x=x; f.y=y;
                        const UINT skip=i && step[i]==8 ? 4 : 0;
                        std::memcpy(&f.before,p+skip,i ? 1 : 4);
                        std::memcpy(&f.after,p+stride+skip,i ? 1 : 4);
                    }
                    ++delta[i];
                }
            }
        }
        readback->Unmap(0,&none);
        return executed;
    }
};
