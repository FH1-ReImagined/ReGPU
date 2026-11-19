#include "gpu/native_scene_converted_private_color_oracle.h"
#include <algorithm>
#include <array>

struct Commands {
    ID3D12Resource* live=reinterpret_cast<ID3D12Resource*>(1);
    ID3D12Resource* tmp=reinterpret_cast<ID3D12Resource*>(2);
    D3D12_RESOURCE_STATES state[2]={D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST};
    std::array<unsigned,12> pixels[2]{{10,11,12,13,20,21,22,23,30,31,32,33},{}};
    unsigned bound=1,draws=0,copies=0,barriers=0,fault=0;
    bool valid=true;
    void OMSetRenderTargets(UINT n,const D3D12_CPU_DESCRIPTOR_HANDLE* v,BOOL,const D3D12_CPU_DESCRIPTOR_HANDLE* z) {
        if (!n) { valid &= v==nullptr && z==nullptr; bound=0; return; }
        valid &= n==1 && v && z && z->ptr==3 && (v->ptr==1 || v->ptr==2);
        bound=unsigned(v->ptr);
        valid &= state[bound-1]==D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    void ResourceBarrier(UINT n,const D3D12_RESOURCE_BARRIER* b) {
        const auto& t=b->Transition;
        valid &= n==1 && !bound && b->Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION &&
            (t.pResource==live || t.pResource==tmp) && t.Subresource==D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        auto& s=state[t.pResource==tmp];
        valid &= s==t.StateBefore;
        s=t.StateAfter; ++barriers;
    }
    void CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* dst,UINT x,UINT y,UINT z,
                           const D3D12_TEXTURE_COPY_LOCATION* src,const D3D12_BOX* box) {
        const bool store=copies++!=0;
        valid &= !bound && dst->pResource==(store ? live : tmp) && src->pResource==(store ? tmp : live) &&
            src->Type==D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX && dst->Type==src->Type && !src->SubresourceIndex &&
            !dst->SubresourceIndex && state[store]==D3D12_RESOURCE_STATE_COPY_SOURCE &&
            state[!store]==D3D12_RESOURCE_STATE_COPY_DEST && box && !z && !box->front && box->back==1 &&
            x==box->left && y==box->top && box->right<=4 && box->bottom<=3;
        if ((fault==1 && !store) || (fault==2 && store)) return;
        for (UINT row=box->top;row<box->bottom;++row)
            for (UINT col=box->left;col<box->right;++col)
                pixels[!store][row*4+col]=pixels[store][row*4+col];
    }
    bool Run(unsigned injected) {
        fault=injected;
        pixels[1].fill(99); // uninitialized private pixels must never reach live output
        auto expected=pixels[0];
        expected[5]+=7;
        const D3D12_BOX box{1,0,0,3,2,1};
        ConvertedPrivateColor::Record(*this,live,tmp,{1},{2},{3},box,[&] {
            valid &= bound==2 && draws++==0;
            pixels[1][5]+=7; // destination-dependent color; other samples retain the LOAD
        });
        return valid && draws==1 && copies==2 && barriers==5 && bound==1 &&
            state[0]==D3D12_RESOURCE_STATE_RENDER_TARGET && pixels[0]==expected && pixels[1][0]==99;
    }
};
// Deliberately exposes only the operations supported by the borrowed draw.
struct BorrowedCommands {
    ID3D12Resource* expected=reinterpret_cast<ID3D12Resource*>(1);
    UINT phase=0;
    bool good=true;
    void CreateRenderTargetView(ID3D12Device*,ID3D12Resource* color,D3D12_CPU_DESCRIPTOR_HANDLE dst) {
        good &= phase==0 && color==expected && dst.ptr==2;
        ++phase;
    }
    void OMSetRenderTargets(UINT count,const D3D12_CPU_DESCRIPTOR_HANDLE* v,BOOL single,
                            const D3D12_CPU_DESCRIPTOR_HANDLE* z) {
        good &= count==1 && !single && v && z && z->ptr==3 &&
            ((phase==1 && v->ptr==2) || (phase==3 && v->ptr==1));
        ++phase;
    }
};
int main() {
    ConvertedPrivateColor::HeapReceipt receipt{};
    receipt.heap=reinterpret_cast<ID3D12DescriptorHeap*>(1);
    receipt.device=reinterpret_cast<ID3D12Device*>(2);
    receipt.view.ptr=3;
    receipt.desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    receipt.desc.NumDescriptors=1;
    if (!receipt.SingleCpuRtv(1)) return 11;
    receipt.desc.NodeMask=1; // The first-node mask on a single-node device.
    if (!receipt.SingleCpuRtv(1) || receipt.SingleCpuRtv(2) || !receipt.Matches(receipt)) return 12;
    auto altered=receipt;
    altered.desc.NodeMask=0;
    if (!altered.SingleCpuRtv(1) || receipt.Matches(altered)) return 13;
    altered.desc.NodeMask=2;
    if (altered.SingleCpuRtv(1)) return 14;
    altered=receipt; ++altered.view.ptr;
    if (receipt.Matches(altered)) return 15;
    altered=receipt; altered.heap=reinterpret_cast<ID3D12DescriptorHeap*>(4);
    if (receipt.Matches(altered)) return 16;
    altered=receipt; altered.device=reinterpret_cast<ID3D12Device*>(4);
    if (receipt.Matches(altered)) return 17;
    altered=receipt; ++altered.desc.NumDescriptors;
    if (receipt.Matches(altered) || altered.SingleCpuRtv(1)) return 18;
    altered=receipt; altered.desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (receipt.Matches(altered) || altered.SingleCpuRtv(1)) return 19;
    altered=receipt; altered.desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (receipt.Matches(altered) || altered.SingleCpuRtv(1)) return 20;
    BorrowedCommands borrowed;
    ConvertedPrivateColor::RecordBorrowed(borrowed,nullptr,borrowed.expected,{1},{2},{3},[&] {
        borrowed.good &= borrowed.phase==2;
        ++borrowed.phase;
    });
    if (!borrowed.good || borrowed.phase!=4) return 10;
    std::array<unsigned char,64> baseline{},privatePixels{},stockPixels{};
    const D3D12_RECT box{0,0,2,2};
    auto compare=[&]{return ConvertedPrivateColorOracle::Compare(baseline.data(),privatePixels.data(),stockPixels.data(),32,box);};
    privatePixels[33]=stockPixels[33]=128; privatePixels[18]=127; // row padding
    auto same=compare();
    if (!same.readable || same.pixels!=4 || same.different || same.stockChanged!=1) return 7;
    privatePixels[33]^=1;
    if (compare().different!=1) return 8;
    privatePixels=stockPixels=baseline;
    if (compare().stockChanged) return 9;
    Commands good,badLoad,badStore;
    if (!good.Run(0) || badLoad.Run(1) || badStore.Run(2)) return 1;
    using C=ConvertedPrivateColor;
    constexpr UINT observed=0x80F20044u;
    if (!C::AcceptState(observed) || C::AcceptState(observed|8) || C::AcceptState(observed|0x100) ||
        C::AcceptState(observed|2) || C::AcceptState(observed&~(1u<<20)) || C::AcceptState(0)) return 2;
    D3D12_RESOURCE_DESC c{};
    c.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; c.Width=2560; c.Height=2048;
    c.DepthOrArraySize=c.MipLevels=c.SampleDesc.Count=1;
    c.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; c.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    auto z=c; z.Format=DXGI_FORMAT_R32G8X24_TYPELESS; z.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_RECT roi{0,0,1920,1080};
    if (!C::Fits(c,z,roi)) return 3;
    z.Width=2501;
    if (C::Fits(c,z,roi)) return 4;
    z.Width=c.Width; roi.right=2501;
    if (C::Fits(c,z,roi)) return 5;
    roi.right=1920; c.SampleDesc.Count=4;
    if (C::Fits(c,z,roi)) return 6;
    return 0;
}
