#pragma once
#include "gpu/m7_benchmark_window.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

namespace regpu::loaded_counts {
constexpr uint64_t kEdgeLagNs = 250000000, kProgressGapNs = 2000000000;
struct Frame {
    uint64_t swap = 0, frame = 0, signal = 0, count = 0;
    uint64_t native_frame = 0, serial = 0, native_fence = 0;
    bool success = false, stopped = false, requested = false;
};
struct Edge { Frame frame; uint64_t now = 0; };
struct Protocol {
    bool started = false, sealed = false, failed = false;
    const char* reason = "open";
    fh1_m7_window::Snapshot host{};
    Edge first{}, last{};
    uint64_t observations = 0, progress = 0, max_gap = 0, max_count = 0;
    void Fail(const char* why) { if (!sealed) { failed = sealed = true; reason = why; } }
    bool Validate(const Frame& f) {
        if (!f.swap || !f.frame || !f.success || !f.signal || f.signal == UINT64_MAX || f.stopped || f.count == UINT64_MAX ||
            (f.count && (!f.native_frame || !f.serial || !f.native_fence || f.native_frame > f.frame || f.native_fence >= f.signal)) ||
            (!f.count && (f.native_frame || f.serial || f.native_fence))) { Fail("frame-or-native-identity"); return false; }
        if (!f.requested && f.count) { Fail("stock-native-count"); return false; }
        return true;
    }
    bool Advance(const Frame& f, uint64_t now) {
        const auto& before = last.frame;
        if (!Validate(f)) return false;
        if (now < last.now || f.swap <= before.swap || f.frame <= before.frame || f.signal <= before.signal ||
            f.requested != first.frame.requested || f.count < before.count ||
            f.native_frame < before.native_frame || f.serial < before.serial || f.native_fence < before.native_fence ||
            (f.count == before.count && (f.native_frame != before.native_frame || f.serial != before.serial || f.native_fence != before.native_fence)) ||
            (f.count > before.count && (f.native_frame <= before.native_frame || f.serial <= before.serial || f.native_fence <= before.native_fence))) {
            Fail("counter-frame-or-mode-order"); return false;
        }
        max_gap = std::max(max_gap, now - progress);
        if (f.count > before.count) progress = now;
        max_count = std::max(max_count, f.count);
        last = {f, now}; ++observations; return true;
    }
    void Observe(const fh1_m7_window::Snapshot& h, const Frame& f, uint64_t now, bool closed) {
        if (sealed) return;
        if (h.version != fh1_m7_window::kVersion || h.bytes != sizeof(h) || h.valid != 1 || h.phase > 2) { Fail("host-phase-or-abi"); return; }
        if (!h.phase) { if (started || h.generation || h.start_ns || h.end_ns) Fail("host-phase-reset"); return; }
        if (!h.generation || !h.start_ns || !now || !closed) { Fail("missing-phase-or-frame-close"); return; }
        if (started && (h.generation != host.generation || h.start_ns != host.start_ns)) { Fail("phase-generation-changed"); return; }
        if (h.phase == 1) {
            if (h.end_ns) { Fail("active-end-value"); return; }
            // The host may publish after the renderer's already-loaded now. Defer, never resample a clock.
            if (now < h.start_ns) return;
            if (!started) {
                if (now - h.start_ns > kEdgeLagNs || !Validate(f)) { Fail("start-edge-lag"); return; }
                host = h; first = last = {f, now}; progress = now; max_count = f.count;
                observations = 1; started = true; return;
            }
            Advance(f, now); return;
        }
        if (!started || h.end_ns < h.start_ns || h.end_ns - h.start_ns < 60000000000ull ||
            h.end_ns - h.start_ns > 62000000000ull) { Fail("missing-start-or-host-duration"); return; }
        if (now < h.end_ns) { Advance(f, now); return; }
        if (now - h.end_ns > kEdgeLagNs || !Advance(f, now)) { Fail("end-edge-lag"); return; }
        host = h;
        if (observations < 100 || (first.frame.requested &&
            (last.frame.count <= first.frame.count || max_gap > kProgressGapNs))) { Fail("insufficient-native-progress"); return; }
        sealed = true; reason = "completed-window";
    }
};
inline bool Enabled() { return fh1_m7_window::Enabled(); }
struct State {
    std::atomic<DWORD> owner{0};
    std::atomic<bool> wrong_owner{false};
    Protocol protocol;
    bool published = false;
    std::string path;
    State() {
        if (const char* value = std::getenv("REGPU_M7_COUNT_OUTPUT")) path = value;
        for (const char* key : {"FH1_M4_ACCEPTED_SWAP_TRACE", "REGPU_ACCEPTED_RETIRE_TRACE", "REGPU_ACCEPTED_RETIRE_THREADED",
                                "REGPU_NATIVE_SCENE_POST_RESOLVE_CENSUS"})
            if (std::getenv(key)) protocol.Fail("trace-or-census-present");
    }
    void Seal() noexcept {
        if (published || !protocol.sealed) return;
        published = true;
        try {
            if (path.empty()) return;
            const std::filesystem::path target(path), partial(path + ".partial"); std::error_code ec;
            if (std::filesystem::exists(target, ec) || ec || std::filesystem::exists(partial, ec) || ec) return;
            FILE* file = std::fopen(partial.string().c_str(), "w"); if (!file) return;
            const auto& p = protocol;
            bool ok = std::fprintf(file, "#protocol=regpu_m7_loaded_counts_v1 sealed=1 failed=%u reason=%s generation=%llu host_start_ns=%llu host_end_ns=%llu edge_lag_ns=%llu progress_gap_ns=%llu observations=%llu max_gap_ns=%llu max_count=%llu worker=%lu\n",
                unsigned(p.failed),p.reason,(unsigned long long)p.host.generation,(unsigned long long)p.host.start_ns,
                (unsigned long long)p.host.end_ns,(unsigned long long)kEdgeLagNs,(unsigned long long)kProgressGapNs,
                (unsigned long long)p.observations,(unsigned long long)p.max_gap,(unsigned long long)p.max_count,owner.load()) > 0;
            ok &= std::fputs("edge,now_ns,swap,frame,signal_fence,count,native_frame,native_serial,native_fence,requested,stopped\n",file)>=0;
            const Edge edges[] = {p.first,p.last};
            for (unsigned i=0;i<2;++i) {
                const auto& e=edges[i]; const auto& f=e.frame;
                ok &= std::fprintf(file,"%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u\n",i,
                    (unsigned long long)e.now,(unsigned long long)f.swap,(unsigned long long)f.frame,
                    (unsigned long long)f.signal,(unsigned long long)f.count,(unsigned long long)f.native_frame,
                    (unsigned long long)f.serial,(unsigned long long)f.native_fence,unsigned(f.requested),unsigned(f.stopped))>0;
            }
            ok &= std::fclose(file)==0;
            if (ok) std::filesystem::rename(partial,target,ec);
        } catch (...) {}
    }
};
inline State& Get() { static State state; return state; }
struct Context {
    uint64_t swap = 0;
    unsigned depth = 0, closes = 0;
    bool invalid = false;
    Frame frame{};
};
inline Context& Current() { static thread_local Context context; return context; }
inline void Begin(uint64_t swap) noexcept {
    if (!Enabled()) return;
    try {
        auto& state=Get(); auto& context=Current();
        DWORD expected=0; const DWORD thread=GetCurrentThreadId();
        const bool owner=state.owner.compare_exchange_strong(expected,thread) || expected==thread;
        if (context.depth) { ++context.depth; state.wrong_owner.store(true); return; }
        context={}; context.depth=1; context.swap=swap; context.invalid=!owner;
        if (!owner) state.wrong_owner.store(true);
    } catch (...) {}
}
inline bool CanSample() {
    if (!Enabled()) return false;
    const auto& c=Current(); return c.depth==1 && !c.invalid;
}
inline void Closed(uint64_t frame,uint64_t signal,bool success,uint64_t count,uint64_t native_frame,
                   uint64_t serial,uint64_t native_fence,bool stopped,bool requested) noexcept {
    if (!CanSample()) return;
    auto& c=Current(); ++c.closes;
    c.frame={c.swap,frame,signal,count,native_frame,serial,native_fence,success,stopped,requested};
}
inline void End(uint64_t now) noexcept {
    if (!Enabled()) return;
    try {
        auto& c=Current();
        if (!c.depth) return;
        if (c.depth>1) { --c.depth; return; }
        c.depth=0;
        if (c.invalid) return; // only the established owner touches Protocol or output
        auto& state=Get();
        if (state.wrong_owner.load()) state.protocol.Fail("worker-owner-or-reentry");
        if (!state.protocol.sealed) {
            static const auto read=reinterpret_cast<fh1_m7_window::ReadFunction>(
                GetProcAddress(GetModuleHandleW(nullptr),"ForzaM7BenchmarkWindowV1"));
            if (!read) state.protocol.Fail("missing-host-phase-export");
            else {
                fh1_m7_window::Snapshot snapshot;
                if (read(fh1_m7_window::kVersion,sizeof(snapshot),&snapshot))
                    state.protocol.Observe(snapshot,c.frame,now,c.closes==1);
            }
        }
        state.Seal();
    } catch (...) {}
}
} // namespace regpu::loaded_counts
