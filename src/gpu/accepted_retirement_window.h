#pragma once
// Isolated opt-in FR02 accepted-swap observation. No resource or guest policy changes.
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace regpu::accepted_retirement {
constexpr uint32_t kHeader = 0xC0056400u;
constexpr uint32_t kMaxRows = 8192;
struct Row {
    uint32_t id = 0;
    uint64_t decoder_swap = 0, frame = 0, signal_fence = 0;
    uint64_t retired = 0, native_frame = 0, native_serial = 0, native_fence = 0;
    uint64_t decode_ns = 0, close_ns = 0, return_ns = 0, native_retire_ns = 0;
};
// Actual runtime policy, independently fixture-testable without a GPU, clock or file.
struct Protocol {
    uint32_t limit = 4096, rows = 0, closes = 0;
    bool active = false, sealed = false, failed = false;
    const char* reason = "open";
    Row current{}, previous{};
    void Fail(const char* why) {
        failed = sealed = true;
        active = false;
        reason = why;
    }
    bool Begin(uint32_t header, uint32_t count, uint32_t vr, uint32_t id,
               uint64_t swap, bool worker, bool nested) {
        if (sealed) return false;
        if (header != kHeader) {
            if (rows || active) Fail("missing-tag");
            return false;
        }
        if (!limit || limit > kMaxRows || count != 6 || vr || !id || id > limit) {
            Fail("packet-or-limit"); return false;
        }
        if (worker || nested || active) { Fail("unsupported-thread-path"); return false; }
        if (id != rows + 1 || !swap || swap == UINT64_MAX || (rows && swap <= previous.decoder_swap)) {
            Fail("duplicate-gap-or-reset"); return false;
        }
        current = {};
        current.id = id;
        current.decoder_swap = swap;
        closes = 0;
        active = true;
        return true;
    }
    void Close(uint64_t frame, uint64_t signal, bool success, uint64_t retired,
               uint64_t native_frame, uint64_t serial, uint64_t native_fence, bool stopped) {
        if (!active || sealed) return;
        if (++closes != 1 || !success || !frame || !signal || signal == UINT64_MAX || stopped || retired == UINT64_MAX ||
            (retired && (!native_frame || !serial || !native_fence || native_frame > frame || native_fence >= signal)) ||
            (!retired && (native_frame || serial || native_fence)) ||
            (rows && (frame <= previous.frame || signal <= previous.signal_fence || retired < previous.retired ||
                      native_frame < previous.native_frame || serial < previous.native_serial || native_fence < previous.native_fence ||
                      (retired == previous.retired && (native_frame != previous.native_frame || serial != previous.native_serial || native_fence != previous.native_fence)) ||
                      (retired > previous.retired && (native_frame <= previous.native_frame || serial <= previous.native_serial || native_fence <= previous.native_fence))))) {
            Fail("close-counter-or-signal"); return;
        }
        current.frame = frame;
        current.signal_fence = signal;
        current.retired = retired;
        current.native_frame = native_frame;
        current.native_serial = serial;
        current.native_fence = native_fence;
    }
    bool End() {
        if (sealed || !active) return false;
        if (closes != 1) { Fail("no-exact-close"); return false; }
        previous = current;
        active = false;
        ++rows;
        if (rows == limit) { sealed = true; reason = "accepted-limit"; }
        return true;
    }
};
inline bool Enabled() {
    static const bool on = [] {
        const char* v = std::getenv("REGPU_ACCEPTED_RETIRE_TRACE");
        return v && !std::strcmp(v, "1");
    }();
    return on;
}
inline uint64_t Now() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct State {
    std::mutex mutex;
    Protocol protocol;
    std::array<Row, kMaxRows> rows{};
    std::thread::id owner{}, native_owner{};
    uint64_t native_count = 0, native_frame = 0, native_serial = 0, native_fence = 0, native_ns = 0;
    std::string path;
    bool published = false;
    State() {
        if (const char* p = std::getenv("REGPU_ACCEPTED_RETIRE_OUTPUT")) path = p;
        const char* value = std::getenv("FH1_M4_ACCEPTED_SWAP_TRACE_SWAPS");
        if (value && *value) {
            char* end = nullptr;
            const unsigned long n = std::strtoul(value, &end, 10);
            protocol.limit = end && !*end && n && n <= kMaxRows ? uint32_t(n) : 0;
        }
    }
    // Caller owns mutex. Final explicit publication precedes runner-owned process termination.
    void Seal() noexcept {
        if (published || !protocol.sealed) return;
        published = true;
        try {
            if (path.empty()) return;
            const std::filesystem::path target(path), partial(path + ".partial");
            std::error_code ec;
            if (std::filesystem::exists(target, ec) || ec || std::filesystem::exists(partial, ec) || ec) return;
            FILE* f = std::fopen(partial.string().c_str(), "w");
            if (!f) return;
            bool ok = std::fprintf(f, "#protocol=regpu_accepted_retirement_v1 sealed=1 failed=%u reason=%s limit=%u rows=%u\n",
                unsigned(protocol.failed), protocol.reason, protocol.limit, protocol.rows) > 0;
            ok &= std::fputs("id,decoder_swap,frame,signal_fence,retired,native_frame,native_serial,native_fence,decode_ns,close_ns,return_ns,native_retire_ns\n", f) >= 0;
            for (uint32_t i = 0; i < protocol.rows; ++i) {
                const auto& r = rows[i];
                ok &= std::fprintf(f, "%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n", r.id,
                    (unsigned long long)r.decoder_swap, (unsigned long long)r.frame, (unsigned long long)r.signal_fence,
                    (unsigned long long)r.retired, (unsigned long long)r.native_frame, (unsigned long long)r.native_serial,
                    (unsigned long long)r.native_fence, (unsigned long long)r.decode_ns,
                    (unsigned long long)r.close_ns, (unsigned long long)r.return_ns, (unsigned long long)r.native_retire_ns) > 0;
            }
            ok &= std::fclose(f) == 0;
            if (ok) std::filesystem::rename(partial, target, ec);
        } catch (...) {
            // Observation failure must never change the stock swap or GPU submission.
        }
    }
};
inline State& Get() { static State state; return state; }
class Scope;
inline Scope*& Current() { static thread_local Scope* value = nullptr; return value; }
class Scope {
public:
    Scope() = default;
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    void Begin(uint32_t header, uint32_t count, uint32_t vr, uint32_t id, uint64_t swap, bool worker) noexcept {
        if (!Enabled()) return;
        try {
            auto& state = Get();
            std::lock_guard lock(state.mutex);
            if (state.protocol.sealed) return;
            if ((state.owner != std::thread::id{} && state.owner != std::this_thread::get_id()) ||
                (state.native_owner != std::thread::id{} && state.native_owner != std::this_thread::get_id())) {
                state.protocol.Fail("thread-changed"); state.Seal(); return;
            }
            if (!state.protocol.Begin(header, count, vr, id, swap, worker, Current() != nullptr)) {
                state.Seal(); return;
            }
            state.owner = std::this_thread::get_id();
            state.protocol.current.decode_ns = Now();
            active_ = true;
            Current() = this;
        } catch (...) {}
    }
    ~Scope() {
        if (!active_) return;
        Current() = nullptr;
        try {
            auto& state = Get();
            std::lock_guard lock(state.mutex);
            if (state.protocol.active && !state.protocol.sealed) {
                state.protocol.current.return_ns = Now();
                const auto row = state.protocol.current;
                if (state.protocol.End()) state.rows[state.protocol.rows - 1] = row;
            }
            state.Seal();
        } catch (...) {}
    }
private:
    bool active_ = false;
};
inline void NativeRetired(uint64_t count, uint64_t frame, uint64_t serial, uint64_t fence, bool stopped) noexcept {
    if (!Enabled()) return;
    try {
        auto& state = Get();
        std::lock_guard lock(state.mutex);
        if (state.protocol.sealed) return;
        const auto thread = std::this_thread::get_id();
        if (stopped || !count || count == UINT64_MAX || !frame || !serial || !fence ||
            count <= state.native_count || frame <= state.native_frame || serial <= state.native_serial || fence <= state.native_fence ||
            (state.owner != std::thread::id{} && state.owner != thread) ||
            (state.native_owner != std::thread::id{} && state.native_owner != thread)) {
            state.protocol.Fail("native-retirement-identity"); state.Seal(); return;
        }
        state.native_count = count;
        state.native_frame = frame;
        state.native_serial = serial;
        state.native_fence = fence;
        state.native_owner = thread;
        state.native_ns = Now();
    } catch (...) {}
}
inline void Closed(uint64_t frame, uint64_t signal, bool success, uint64_t retired,
                   uint64_t native_frame, uint64_t serial, uint64_t native_fence, bool stopped) noexcept {
    if (!Enabled() || !Current()) return;
    try {
        auto& state = Get();
        std::lock_guard lock(state.mutex);
        if (retired != state.native_count || native_frame != state.native_frame ||
            serial != state.native_serial || native_fence != state.native_fence) {
            state.protocol.Fail("native-retirement-receipt-missing");
        } else {
            state.protocol.Close(frame, signal, success, retired, native_frame, serial, native_fence, stopped);
        }
        if (state.protocol.active) {
            state.protocol.current.native_retire_ns = state.native_ns;
            state.protocol.current.close_ns = Now();
        }
        state.Seal();
    } catch (...) {}
}
} // namespace regpu::accepted_retirement
