#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace fh1_m7_window {
constexpr uint32_t kVersion = 1;
struct Snapshot {
    uint32_t version = kVersion, bytes = 40;
    uint64_t generation = 0, start_ns = 0, end_ns = 0;
    uint32_t phase = 0, valid = 1; // 0 waiting, 1 measuring, 2 completed, 3 refused
};
static_assert(sizeof(Snapshot) == 40 && alignof(Snapshot) == 8);
static_assert(offsetof(Snapshot, generation) == 8 && offsetof(Snapshot, phase) == 32);
using ReadFunction = uint32_t (*)(uint32_t, uint32_t, Snapshot*);
inline bool Enabled() {
    static const bool on = [] {
        const char* value = std::getenv("REGPU_M7_COUNT_RECEIPT");
        return value && !std::strcmp(value, "1");
    }();
    return on;
}
// Two host phase edges only. Readers never wait/retry; every payload member is atomic.
struct Publisher {
    std::atomic_flag writer = ATOMIC_FLAG_INIT;
    std::atomic<uint64_t> sequence{0}, generation{0}, start{0}, end{0};
    std::atomic<uint32_t> phase{0};
    std::atomic<bool> poisoned{false};
    bool Enter() {
        if (writer.test_and_set(std::memory_order_acquire)) { poisoned.store(true); return false; }
        if (sequence.load() > UINT64_MAX - 2) {
            poisoned.store(true); writer.clear(std::memory_order_release); return false;
        }
        sequence.fetch_add(1);
        return true;
    }
    void Leave() {
        sequence.fetch_add(1);
        writer.clear(std::memory_order_release);
    }
    void Begin(uint64_t now) {
        if (!Enter()) return;
        const auto old = generation.load();
        if (!now || old == UINT64_MAX || phase.load() == 1) {
            poisoned.store(true); Leave(); return;
        }
        generation.store(old + 1);
        start.store(now); end.store(0);
        phase.store(1); Leave();
    }
    void End(bool valid, uint64_t now) {
        if (!Enter()) return;
        if (!valid || !generation.load() || phase.load() != 1 ||
            !now || now < start.load()) {
            poisoned.store(true); phase.store(3); Leave(); return;
        }
        end.store(now); phase.store(2); Leave();
    }
    bool Read(uint32_t version, uint32_t bytes, Snapshot* output) const {
        if (!output || version != kVersion || bytes != sizeof(Snapshot)) return false;
        if (poisoned.load()) { *output = {}; output->phase = 3; output->valid = 0; return true; }
        const auto before = sequence.load();
        if (before & 1) return false;
        Snapshot snapshot;
        snapshot.generation = generation.load();
        snapshot.start_ns = start.load();
        snapshot.end_ns = end.load();
        snapshot.phase = phase.load();
        const auto after = sequence.load();
        if (before != after || (after & 1) || poisoned.load()) return false;
        *output = snapshot; return true;
    }
};
inline Publisher& Get() { static Publisher publisher; return publisher; }
} // namespace fh1_m7_window
