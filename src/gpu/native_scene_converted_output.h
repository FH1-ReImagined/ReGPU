#pragma once
#include <d3d12.h>
#include "gpu/native_scene_converted_private_color_oracle.h"
#include <wrl/client.h>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <atomic>

// Logical banks 0..3 rotate as usual. Bank 4 is the one-shot immutable clone.
struct ConvertedSamplerBanks {
    static constexpr UINT slots=8, spill=5, capacity=(spill+1)*slots;
    std::atomic<int> protectedBank{-1}, relocatedBank{-1};

    // Called by the normal serialized writer before filling an entire bank.
    UINT ForWrite(UINT logical) {
        if (protectedBank.load()==int(logical)) {
            relocatedBank.store(int(logical));
            return spill;
        }
        if (relocatedBank.load()==int(logical)) relocatedBank.store(-1);
        return logical;
    }
    UINT ForRead(UINT logical) const {
        return relocatedBank.load()==int(logical) ? spill : logical;
    }
    void Protect(UINT logical) { protectedBank.store(int(logical)); }
    void Retire() { protectedBank.store(-1); } // leave spill intact until a new full write
};

struct ImmutableDrawSamplers {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> samplerHeap;
    D3D12_GPU_DESCRIPTOR_HANDLE samplerOriginal{},samplerImmutable{};
    UINT samplerBank=0;
    UINT64 samplerSerial=0;
    bool samplerLease=false;
};

struct ConvertedOutput : ImmutableDrawSamplers {
    static bool Requested() {
        static const bool on=[] {
            const auto* e=std::getenv("REGPU_CONVERTED_OUTPUT_LEASE");
            const auto* census=std::getenv("REGPU_NATIVE_SCENE_POST_RESOLVE_CENSUS");
            const bool ordinaryFrame=!std::getenv("REGPU_NATIVE_SCENE_PRIVATE_PAIR_FRAME") || (census && !std::strcmp(census,"1"));
            return ordinaryFrame && e && !std::strcmp(e,"1") &&
                !std::getenv("REGPU_NATIVE_SCENE_PRIVATE_PAIR_DEFER") &&
                !std::getenv("REGPU_NATIVE_SCENE_TEXTURED_COMMON_GRID") &&
                !std::getenv("REGPU_NATIVE_SCENE_FIRST_TEXTURED_STOCK_SAMPLES") &&
                !std::getenv("REGPU_NATIVE_SCENE_STOCK_DSV_DISCOVERY") &&
                !std::getenv("REGPU_NATIVE_SCENE_STOCK_QUERY_CENSUS") &&
                !std::getenv("REGPU_NATIVE_SCENE_NO_CLEAR_QUERY_CENSUS");
        }();
        return on;
    }
    static bool PrivateColorRequested() {
        static const bool on=[] {
            const auto* value=std::getenv("REGPU_CONVERTED_OUTPUT_PRIVATE_COLOR");
            return value && !std::strcmp(value,"1") && Requested();
        }();
        return on;
    }
    static bool BorrowedColorRequested() {
        static const bool on=[] {
            const char* value=std::getenv("REGPU_CONVERTED_OUTPUT_BORROWED_RTV");
            return value && !std::strcmp(value,"1") && PrivateColorRequested();
        }();
        return on;
    }
    static bool PrivateColorOracleRequested() {
        static const bool on=[] {
            const char* value=std::getenv("REGPU_CONVERTED_OUTPUT_PRIVATE_COLOR_ORACLE");
            return value && !std::strcmp(value,"1") && PrivateColorRequested();
        }();
        return on;
    }
    static bool RecurringBorrowRequested() {
        static const bool on=[] {
            const char* value=std::getenv("REGPU_CONVERTED_OUTPUT_RECURRING_RTV");
            return value && !std::strcmp(value,"1") && BorrowedColorRequested() && !PrivateColorOracleRequested();
        }();
        return on;
    }
    ConvertedPrivateColor privateColor;
    ConvertedPrivateColorOracle colorOracle;
    enum class State { empty, pending, claimed, bound };
    State state=State::empty;
    Microsoft::WRL::ComPtr<ID3D12Resource> image;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap;
    UINT64 frame=0,epoch=0,serial=0,draw=0,fence=0;
    UINT base=0,width=0,height=0,descriptor=0;
    bool failed=false;

    void Cancel() {
        if (state==State::pending || state==State::claimed) *this={};
    }
    bool Claim() {
        if (state==State::claimed) { Cancel(); return false; }
        if (state!=State::pending) return false;
        state=State::claimed;
        return true;
    }
    bool Retired(UINT64 completed) const {
        return state==State::bound && !failed && fence && completed!=UINT64_MAX && completed>=fence;
    }
    struct Scope {
        ConvertedOutput* owner=nullptr;
        UINT64 serial=0;
        ~Scope() { if (owner && owner->serial==serial) owner->Cancel(); }
    };
};

// Master recorder only. Retain the two descriptor heaps across slot reuse,
// never the previous publication or its sampled image. Reserved samplers are
// written only by the original Take, even across all repeated draws.
struct RecurringBorrowedRtv {
    static constexpr UINT64 detailedRetirements=9;
    const ConvertedOutput* slot=nullptr;
    UINT64 retired=0;
    UINT64 frame=0,epoch=0,serial=0,fence=0;
    ImmutableDrawSamplers sampler;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv;
    ConvertedPrivateColor::HeapReceipt heapReceipt;
    bool ready=false;
    bool stopped=false;
    bool DetailNext() const { return retired<detailedRetirements; }
    void Stop() {
        stopped=true; ready=false; sampler={}; heapReceipt={}; rtv.Reset();
    }

    bool CanStage(const ConvertedOutput* next,UINT64 nextFrame,UINT64 nextEpoch,UINT64 nextSerial) const {
        return !stopped && ready && retired && slot==next && frame<nextFrame &&
            epoch<nextEpoch && serial<nextSerial;
    }
    void ReleaseCompleted(const ConvertedOutput& owner,UINT64 completed) {
        ready=false;
        if (stopped || !owner.Retired(completed) || !owner.privateColor.borrowed || !owner.privateColor.rtv ||
            owner.privateColor.heapReceipt.heap!=owner.privateColor.rtv.Get() ||
            !owner.samplerLease || !owner.samplerHeap || !owner.samplerSerial || owner.fence<=fence ||
            (slot && slot!=&owner)) {
            Stop(); return;
        }
        if (retired!=UINT64_MAX) ++retired;
        slot=&owner; frame=owner.frame; epoch=owner.epoch; serial=owner.serial; fence=owner.fence;
        sampler=static_cast<const ImmutableDrawSamplers&>(owner);
        rtv=owner.privateColor.rtv;
        heapReceipt=owner.privateColor.heapReceipt;
        ready=true;
    }
};
