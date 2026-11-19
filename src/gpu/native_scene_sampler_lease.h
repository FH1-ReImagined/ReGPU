#pragma once
#include <array>
#include <cstdint>
#include <limits>

// CPU-only receipt. Caller serializes ALL receipt access with g_samplerMutex.
// Never reads descriptors back from a shader-visible heap. One attempt per process.
namespace regpu::native_scene {
template<class Descriptor> struct SamplerLeaseReceipts {
    static constexpr uint32_t banks=4, slots=8, reserved=32, capacity=40;
    struct Receipt { std::array<Descriptor,slots> descriptors{}; uint64_t owner=0, serial=0; };
    std::array<Receipt,banks> receipts{};
    uint64_t next=0;
    bool attempted=false, poisoned=false;
    void Record(uint64_t owner,uint32_t bank,const std::array<Descriptor,slots>& values) {
        if(!owner || bank>=banks || next==std::numeric_limits<uint64_t>::max()) { poisoned=true;return; }
        receipts[bank]={values,owner,++next};
    }
    static bool Bank(uint64_t begin,uint64_t table,uint32_t stride,uint32_t& bank) {
        if(!begin || !stride || table<begin) return false;
        const uint64_t delta=table-begin, step=uint64_t(slots)*stride;
        if(delta%step || delta/step>=banks) return false;
        bank=uint32_t(delta/step);return true;
    }
    bool Take(uint64_t owner,uint32_t bank,Receipt& out) {
        if(attempted) return false;
        attempted=true; // Refusal also consumes the sole attempt. Never overwrite reserved slots.
        if(poisoned || !owner || bank>=banks || !receipts[bank].serial || receipts[bank].owner!=owner) return false;
        out=receipts[bank];return true;
    }
    bool StillCurrent(uint64_t owner,uint32_t bank,uint64_t serial) const {
        return attempted && !poisoned && owner && bank<banks && serial &&
            receipts[bank].owner==owner && receipts[bank].serial==serial;
    }
};
}
