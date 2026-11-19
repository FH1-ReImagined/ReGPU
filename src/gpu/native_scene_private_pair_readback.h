#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <utility>

// Single frame-owned diagnostic only. No renderer resource is adopted.
struct PrivatePairReadback {
    using Resource = Microsoft::WRL::ComPtr<ID3D12Resource>;
    struct Image { D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[2]{}; UINT rows[2]{}; UINT64 rowBytes[2]{}; UINT planes=0; } image[4];
    Resource buffer;
    UINT64 bytes=0, frame=0;
    UINT sw=0,sh=0,dw=0,dh=0;
    struct Result { bool mapped=false; uint64_t hash[4]{}; unsigned compared=0, depthMismatch=0, stencilMismatch=0, invalid=0, colorCompared=0, colorMismatch=0, colorInvalid=0; };
    static constexpr UINT64 kMaxBytes=256ull*1024*1024;
    bool Record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* color,
                ID3D12Resource* depth, ID3D12Resource* outColor, ID3D12Resource* outDepth,
                UINT width,UINT height,UINT outputWidth,UINT outputHeight,UINT64 frameId,UINT64 maxBytes=kMaxBytes) {
        if (!device || !list || !color || !depth || !outColor || !outDepth || buffer || !width || !height || !outputWidth || !outputHeight || !maxBytes || maxBytes>kMaxBytes) return false;
        ID3D12Resource* resources[]={color,depth,outColor,outDepth};
        const auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        const D3D12_RESOURCE_STATES states[]={D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_DEPTH_WRITE,read,D3D12_RESOURCE_STATE_DEPTH_WRITE};
        UINT64 total=0;
        for (unsigned i=0;i<4;++i) {
            auto desc=resources[i]->GetDesc(); image[i].planes=(i&1)?2:1;
            if (desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize!=1 || desc.MipLevels!=1 || desc.SampleDesc.Count!=1 ||
                desc.Width < ((i<2)?width:outputWidth) || desc.Height < ((i<2)?height:outputHeight) ||
                ((i&1) ? desc.Format!=DXGI_FORMAT_R32G8X24_TYPELESS : desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)) return false;
            total=(total+511)&~UINT64(511); UINT64 required=0;
            device->GetCopyableFootprints(&desc,0,image[i].planes,total,image[i].fp,image[i].rows,image[i].rowBytes,&required);
            if (!required || required==UINT64_MAX || total>maxBytes) return false;
            for(UINT plane=0;plane<image[i].planes;++plane) {
                const auto& fp=image[i].fp[plane];const UINT64 extent=UINT64(fp.Footprint.RowPitch)*image[i].rows[plane];
                if(fp.Offset>maxBytes || extent>maxBytes-fp.Offset) return false;
                if(fp.Offset+extent>total) total=fp.Offset+extent;
            }
        }
        if (!total || total>maxBytes) return false;
        bytes=total; // published as the requested size even if allocation fails
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=total;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Resource target;if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&target)))) return false;
        // Allocate and validate every footprint before recording the first barrier.
        for (unsigned i=0;i<4;++i) for(UINT p=0;p<image[i].planes;++p) {
            const auto& fp=image[i].fp[p];const UINT64 extent=UINT64(fp.Footprint.RowPitch)*image[i].rows[p];
            if(fp.Offset>total || extent>total-fp.Offset || image[i].rowBytes[p]>fp.Footprint.RowPitch) return false;
        }
        for(unsigned i=0;i<4;++i) {
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resources[i],D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,states[i],D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);
            for(UINT p=0;p<image[i].planes;++p) {D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=resources[i];src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=p;D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=target.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=image[i].fp[p];list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
            b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.StateAfter=states[i];list->ResourceBarrier(1,&b);
        }
        buffer=std::move(target);bytes=total;frame=frameId;sw=width;sh=height;dw=outputWidth;dh=outputHeight;return true;
    }
    static float Half(UINT16 h) {
        const unsigned exponent=(h>>10)&31, mantissa=h&1023;
        if(exponent==31) return mantissa ? NAN : ((h&0x8000) ? -INFINITY : INFINITY);
        const float value=exponent ? std::ldexp(float(1024+mantissa),int(exponent)-25) : std::ldexp(float(mantissa),-24);
        return (h&0x8000) ? -value : value;
    }
    Result Finish() {
        Result out{};if(!buffer)return out;unsigned char* p=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};
        if(FAILED(buffer->Map(0,&range,reinterpret_cast<void**>(&p))) || !p) {buffer.Reset();return out;}
        out.mapped=true;
        // FNV-1a over meaningful rows only, avoiding uninitialized footprint padding.
        for(unsigned i=0;i<4;++i){uint64_t h=14695981039346656037ull;for(UINT plane=0;plane<image[i].planes;++plane)for(UINT y=0;y<image[i].rows[plane];++y){const auto* row=p+image[i].fp[plane].Offset+UINT64(y)*image[i].fp[plane].Footprint.RowPitch;for(UINT64 x=0;x<image[i].rowBytes[plane];++x){h^=row[x];h*=1099511628211ull;}}out.hash[i]=h;}
        const auto& a=image[1];const auto& b=image[3];
        const UINT as=UINT(a.rowBytes[1]/a.fp[1].Footprint.Width),bs=UINT(b.rowBytes[1]/b.fp[1].Footprint.Width);
        const UINT ad=UINT(a.rowBytes[0]/a.fp[0].Footprint.Width),bd=UINT(b.rowBytes[0]/b.fp[0].Footprint.Width);
        if ((as!=1&&as!=8)||(bs!=1&&bs!=8)||(ad!=4&&ad!=8)||(bd!=4&&bd!=8)) ++out.invalid;
        else for(UINT iy=0;iy<16;++iy)for(UINT ix=0;ix<16;++ix){
            const UINT x=UINT((UINT64(ix)*(dw-1))/15),y=UINT((UINT64(iy)*(dh-1))/15);
            const UINT sx=UINT((double(x)+0.5)*sw/dw),sy=UINT((double(y)+0.5)*sh/dh);
            auto ptr=[&](const Image& im,UINT plane,UINT xx,UINT yy,UINT stride){return p+im.fp[plane].Offset+UINT64(yy)*im.fp[plane].Footprint.RowPitch+UINT64(xx)*stride;};
            float za=0,zb=0;std::memcpy(&za,ptr(a,0,sx,sy,ad),4);std::memcpy(&zb,ptr(b,0,x,y,bd),4);
            if(!std::isfinite(za)||!std::isfinite(zb)){++out.invalid;continue;}
            ++out.compared;if(za!=zb)++out.depthMismatch;
            if(ptr(a,1,sx,sy,as)[as==8?4:0]!=ptr(b,1,x,y,bs)[bs==8?4:0])++out.stencilMismatch;
        }
        // Compare the actual half-float RGBA output with the shader's clamped bilinear formula.
        const auto& ca=image[0];const auto& cb=image[2];
        if(ca.rowBytes[0]/ca.fp[0].Footprint.Width!=8 || cb.rowBytes[0]/cb.fp[0].Footprint.Width!=8) ++out.colorInvalid;
        else for(UINT iy=0;iy<16;++iy)for(UINT ix=0;ix<16;++ix){
            const UINT x=UINT((UINT64(ix)*(dw-1))/15),y=UINT((UINT64(iy)*(dh-1))/15);
            const double fx=std::clamp((double(x)+0.5)*sw/dw-0.5,0.0,double(sw-1));
            const double fy=std::clamp((double(y)+0.5)*sh/dh-0.5,0.0,double(sh-1));
            const UINT x0=UINT(fx),y0=UINT(fy),x1=std::min(x0+1,sw-1),y1=std::min(y0+1,sh-1);
            auto color=[&](const Image& im,UINT xx,UINT yy,UINT c){UINT16 h;std::memcpy(&h,p+im.fp[0].Offset+UINT64(yy)*im.fp[0].Footprint.RowPitch+UINT64(xx)*8+c*2,2);return Half(h);};
            for(UINT c=0;c<4;++c){const float a=color(ca,x0,y0,c),b=color(ca,x1,y0,c),d=color(ca,x0,y1,c),e=color(ca,x1,y1,c),got=color(cb,x,y,c);
                if(!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(d)||!std::isfinite(e)||!std::isfinite(got)){++out.colorInvalid;continue;}
                const double tx=fx-x0,ty=fy-y0,expected=(a+(b-a)*tx)*(1-ty)+(d+(e-d)*tx)*ty;
                ++out.colorCompared;if(std::abs(got-expected)>0.003*std::max(1.0,std::abs(expected)))++out.colorMismatch;
            }
        }
        D3D12_RANGE none{0,0};buffer->Unmap(0,&none);buffer.Reset();return out;
    }
};

// The before and after readbacks are owned by the same frame slot. Rectangles
// are expressed on their respective grids; compare only meaningful plane bytes.
struct PrivatePairedDelta {
    struct Counts { UINT64 pixels=0, color=0, depth=0, stencil=0; };
    struct Result { bool readable=false; Counts side[2]{}; };
    struct Plane {
        const unsigned char* base=nullptr;
        UINT pitch=0, stride=0, offset=0, width=0;
    };

    static bool GetPlane(const PrivatePairReadback& rb, const unsigned char* data,
                         UINT image, UINT plane, D3D12_RECT rect, Plane& out) {
        if (!data || image>=4 || plane>=rb.image[image].planes || plane>=2 ||
            rect.left<0 || rect.top<0 || rect.right<=rect.left || rect.bottom<=rect.top) return false;
        const auto& im=rb.image[image];
        const auto& fp=im.fp[plane];
        const UINT w=fp.Footprint.Width, pitch=fp.Footprint.RowPitch;
        if (!w || !pitch || UINT(rect.right)>w || UINT(rect.bottom)>im.rows[plane] ||
            im.rowBytes[plane]>pitch || im.rowBytes[plane]%w || fp.Offset>rb.bytes ||
            im.rows[plane]>(rb.bytes-fp.Offset)/pitch) return false;
        const UINT stride=UINT(im.rowBytes[plane]/w);
        UINT offset=0, width=8;
        if (!(image&1)) {
            if (stride!=8 || plane) return false;
        } else if (plane==0) {
            if (stride!=4 && stride!=8) return false;
            width=4;
        } else {
            if (stride!=1 && stride!=8) return false;
            offset=stride==8 ? 4 : 0;
            width=1;
        }
        out={data+fp.Offset,pitch,stride,offset,width};
        return true;
    }

    static bool Count(const PrivatePairReadback& before, const unsigned char* a,
                      const PrivatePairReadback& after, const unsigned char* b,
                      UINT image, UINT plane, D3D12_RECT rect, UINT64& count) {
        Plane x,y;
        if (!GetPlane(before,a,image,plane,rect,x) || !GetPlane(after,b,image,plane,rect,y) ||
            x.width!=y.width) return false;
        UINT64 changed=0;
        for (LONG row=rect.top; row<rect.bottom; ++row) {
            const auto* left=x.base+UINT64(row)*x.pitch+UINT64(rect.left)*x.stride+x.offset;
            const auto* right=y.base+UINT64(row)*y.pitch+UINT64(rect.left)*y.stride+y.offset;
            for (LONG col=rect.left; col<rect.right; ++col)
                changed+=std::memcmp(left+UINT64(col-rect.left)*x.stride,
                                     right+UINT64(col-rect.left)*y.stride,x.width)!=0;
        }
        count=changed;
        return true;
    }

    static Result Compare(const PrivatePairReadback& before, const PrivatePairReadback& after,
                          UINT64 frame, D3D12_RECT stock, D3D12_RECT output) {
        if (!before.buffer || !after.buffer || before.buffer.Get()==after.buffer.Get() ||
            before.frame!=frame || after.frame!=frame || before.sw!=after.sw || before.sh!=after.sh ||
            before.dw!=after.dw || before.dh!=after.dh || !before.bytes || !after.bytes ||
            before.bytes>PrivatePairReadback::kMaxBytes ||
            after.bytes>PrivatePairReadback::kMaxBytes-before.bytes ||
            UINT(stock.right)>before.sw || UINT(stock.bottom)>before.sh ||
            UINT(output.right)>before.dw || UINT(output.bottom)>before.dh) return {};
        unsigned char *a=nullptr,*b=nullptr;
        const D3D12_RANGE ra{0,SIZE_T(before.bytes)}, rb{0,SIZE_T(after.bytes)}, none{0,0};
        if(FAILED(before.buffer->Map(0,&ra,reinterpret_cast<void**>(&a)))) return {};
        Result result;
        if(SUCCEEDED(after.buffer->Map(0,&rb,reinterpret_cast<void**>(&b)))) {
            bool valid=true;
            for(UINT side=0; side<2 && valid; ++side) {
                const auto rect=side ? output : stock;
                auto& n=result.side[side];
                valid=Count(before,a,after,b,2*side,0,rect,n.color) &&
                      Count(before,a,after,b,2*side+1,0,rect,n.depth) &&
                      Count(before,a,after,b,2*side+1,1,rect,n.stencil);
                if(valid) n.pixels=UINT64(rect.right-rect.left)*UINT64(rect.bottom-rect.top);
            }
            result.readable=valid;
            after.buffer->Unmap(0,&none);
        }
        before.buffer->Unmap(0,&none);
        return result.readable ? result : Result{};
    }
};

// One RGBA16F scissor box taken after the private clear and immediately before
// the private draw. The four-image readback already contains the complete after image.
struct PrivateDrawColorBefore {
    PrivatePairReadback::Resource buffer;
    ID3D12Resource* pairedAfter = nullptr; // owned by the same FrameResources
    D3D12_RECT rect{};
    UINT pitch = 0;
    UINT64 bytes = 0, frame = 0, draw = 0;

    bool Record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* color,
                D3D12_RECT box, UINT64 frameId, UINT64 drawId, UINT64 available) {
        if (!device || !list || !color || buffer || box.left < 0 || box.top < 0 ||
            box.right <= box.left || box.bottom <= box.top) return false;
        const auto desc = color->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize != 1 ||
            desc.MipLevels != 1 || desc.SampleDesc.Count != 1 ||
            desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT ||
            UINT64(box.right) > desc.Width || UINT(box.bottom) > desc.Height) return false;
        const UINT width = UINT(box.right - box.left), height = UINT(box.bottom - box.top);
        const UINT64 rowPitch = (UINT64(width) * 8 + 255) & ~UINT64(255);
        if (rowPitch > UINT32_MAX || rowPitch > available / height ||
            available > PrivatePairReadback::kMaxBytes) return false;
        const UINT64 requested = rowPitch * height;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC dstDesc{};
        dstDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        dstDesc.Width = requested;
        dstDesc.Height = dstDesc.DepthOrArraySize = dstDesc.MipLevels = dstDesc.SampleDesc.Count = 1;
        dstDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &dstDesc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer)))) return false;
        const auto read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {color, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, read, D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
        src.pResource = color;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = buffer.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint = {desc.Format, width, height, 1, UINT(rowPitch)};
        const D3D12_BOX sourceBox{UINT(box.left), UINT(box.top), 0, UINT(box.right), UINT(box.bottom), 1};
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, &sourceBox);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &barrier);
        rect = box;
        pitch = UINT(rowPitch);
        bytes = requested;
        frame = frameId;
        draw = drawId;
        return true;
    }

    struct Result { bool readable = false; UINT64 pixels = 0, changed = 0; };
    // The first pointer starts at the compact box, the second at the full image.
    static Result CompareRows(const unsigned char* before, UINT beforePitch, const unsigned char* after,
                              UINT afterPitch, D3D12_RECT box) {
        Result result;
        if (!before || !after || box.left < 0 || box.top < 0 || box.right <= box.left ||
            box.bottom <= box.top || UINT64(box.right - box.left) * 8 > beforePitch ||
            UINT64(box.right) * 8 > afterPitch) return result;
        result.readable = true;
        for (LONG y = box.top; y < box.bottom; ++y) {
            const auto* a = before + UINT64(y - box.top) * beforePitch;
            const auto* b = after + UINT64(y) * afterPitch + UINT64(box.left) * 8;
            for (LONG x = box.left; x < box.right; ++x, a += 8, b += 8) {
                ++result.pixels;
                if (std::memcmp(a, b, 8) != 0) ++result.changed;
            }
        }
        return result;
    }

    Result Compare(const PrivatePairReadback& after) const {
        Result result;
        if (!buffer || !pairedAfter || pairedAfter != after.buffer.Get() || frame != after.frame ||
            UINT(rect.right) > after.dw || UINT(rect.bottom) > after.dh) return result;
        const auto& image = after.image[2]; // private output color, never the stock source
        const auto& fp = image.fp[0];
        if (image.planes != 1 || fp.Footprint.Format != DXGI_FORMAT_R16G16B16A16_FLOAT ||
            UINT(rect.bottom) > image.rows[0] || UINT64(rect.right) * 8 > image.rowBytes[0] ||
            image.rowBytes[0] > fp.Footprint.RowPitch || fp.Offset > after.bytes ||
            UINT64(image.rows[0]) * fp.Footprint.RowPitch > after.bytes - fp.Offset) return result;
        unsigned char *beforeBytes = nullptr, *afterBytes = nullptr;
        const D3D12_RANGE beforeRange{0, SIZE_T(bytes)}, afterRange{SIZE_T(fp.Offset),
            SIZE_T(fp.Offset + UINT64(image.rows[0]) * fp.Footprint.RowPitch)}, none{0, 0};
        if (FAILED(buffer->Map(0, &beforeRange, reinterpret_cast<void**>(&beforeBytes)))) return result;
        if (SUCCEEDED(after.buffer->Map(0, &afterRange, reinterpret_cast<void**>(&afterBytes)))) {
            if (afterBytes) result = CompareRows(beforeBytes, pitch, afterBytes + fp.Offset,
                                                 fp.Footprint.RowPitch, rect);
            after.buffer->Unmap(0, &none);
        }
        buffer->Unmap(0, &none);
        return result;
    }
};
