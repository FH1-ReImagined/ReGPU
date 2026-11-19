#pragma once
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>
#include "log.h"

struct StockPostResolve {
    static bool Enabled() {
        static const bool enabled=[] {
            const char* value=std::getenv("REGPU_NATIVE_SCENE_POST_RESOLVE_CENSUS");
            return value && !std::strcmp(value,"1") &&
                !std::getenv("REGPU_NATIVE_SCENE_PRIVATE_PAIR_DEFER") &&
                !std::getenv("REGPU_NATIVE_SCENE_STOCK_DSV_DISCOVERY") &&
                !std::getenv("REGPU_NATIVE_SCENE_STOCK_QUERY_CENSUS") &&
                !std::getenv("REGPU_NATIVE_SCENE_NO_CLEAR_QUERY_CENSUS") &&
                !std::getenv("REGPU_NATIVE_SCENE_FIRST_TEXTURED_STOCK_SAMPLES") &&
                !std::getenv("REGPU_NATIVE_SCENE_TEXTURED_COMMON_GRID");
        }();
        return enabled;
    }
    static constexpr UINT64 first=5040, last=5047;
    static bool InWindow(UINT64 f) { return f>=first && f<=last; }
    static bool HasRoom(UINT64 f,UINT perFrame,UINT all) { return InWindow(f) && perFrame<2 && all<16; }
    struct Ticket {
        UINT id=0,ordinal=0;
        UINT64 frame=0,draw=0,epoch=0,serial=0;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{},dsv{};
        Microsoft::WRL::ComPtr<ID3D12Resource> color,depth,publication;
        bool explicitFrame=false,phase=false,claimed=false;
    } live;
    UINT perFrame=0,all=0,closed=0;
    UINT64 quotaFrame=UINT64_MAX;
    bool done=false;
    void Finish(const char* status) {
        if (!live.id) return;
        Log("stock postresolve end: id=%u frame=%llu draw=%llu status=%s",live.id,live.frame,live.draw,status);
        ++closed;
        live={};
    }
    struct DrawScope {
        StockPostResolve* owner=nullptr;
        UINT id=0;
        ~DrawScope() { if (id && owner->live.id==id) owner->Finish("next_draw_not_issued"); }
    };
};
