#pragma once
// Separate opt-in default-draw-thread witness. No guest or submission policy changes.
#include "accepted_retirement_window.h"
#include <limits>

namespace regpu::accepted_threaded {
constexpr uint32_t kPayloadWords = 6;
struct Ticket {
    uint32_t id = 0;
    uint64_t generation = 0;
    explicit operator bool() const { return id && generation; }
};
struct Row {
    Ticket ticket{};
    uint64_t decoder_swap = 0, queue_position = 0;
    uint64_t decode_ns = 0, enqueue_ns = 0, paced_ns = 0;
    uint64_t worker_ns = 0, close_ns = 0, worker_return_ns = 0;
    uint64_t frame = 0, signal = 0;
    uint64_t retired = 0, native_frame = 0, native_serial = 0, native_fence = 0, native_ns = 0;
    bool queued = false, paced = false, started = false, ended = false, closed = false;
};
// Fixed storage, never reset/reused in the process. All methods require State::mutex.
struct Protocol {
    uint32_t limit = 4096, issued = 0, completed = 0;
    uint64_t generation = 0;
    bool failed = false;
    const char* reason = "open";
    std::array<Row, accepted_retirement::kMaxRows> rows{};
    bool Terminal() const { return failed || (limit && limit <= rows.size() && issued == limit && completed == limit); }
    void Fail(const char* why) { if (!failed && !Terminal()) { failed = true; reason = why; } }
    Row* Find(Ticket ticket) {
        if (!ticket || ticket.id > issued) { Fail("unknown-ticket"); return nullptr; }
        auto& row = rows[ticket.id - 1];
        if (row.ticket.generation != ticket.generation) { Fail("stale-generation"); return nullptr; }
        return &row;
    }
    Ticket Begin(uint32_t header, uint32_t count, uint32_t vr, uint32_t id,
                 uint64_t swap, bool worker, bool nested, uint64_t now) {
        if (Terminal()) return {};
        // A zero limit is invalid configuration, not an exhausted accepted window.
        // Untagged prestart packets retain their original no-op semantics.
        if (header == accepted_retirement::kHeader && (!limit || limit > rows.size())) {
            Fail("packet-or-limit"); return {};
        }
        if (issued == limit) return {};
        if (header != accepted_retirement::kHeader) {
            if (issued) Fail("missing-tag");
            return {};
        }
        if (!limit || limit > rows.size() || count != 6 || vr || !id || id != issued + 1 ||
            id > limit || !swap || swap == UINT64_MAX || !now || !worker || nested ||
            generation == UINT64_MAX || (issued && (swap <= rows[issued - 1].decoder_swap || !rows[issued - 1].paced || now < rows[issued - 1].paced_ns))) {
            Fail("packet-thread-or-generation"); return {};
        }
        Ticket ticket{id, ++generation};
        auto& row = rows[issued++];
        row.ticket = ticket;
        row.decoder_swap = swap;
        row.decode_ns = now;
        return ticket;
    }
    void Bind(Ticket ticket, uint64_t position, uint64_t capacity, uint64_t now) {
        if (Terminal()) return;
        auto* row = Find(ticket); if (!row || failed) return;
        if (row->queued || !capacity || position > UINT64_MAX - capacity ||
            position % capacity + kPayloadWords + 1 > capacity || now < row->decode_ns ||
            (ticket.id > 1 && position <= rows[ticket.id - 2].queue_position)) {
            Fail("queue-position-or-duplicate"); return;
        }
        row->queue_position = position; row->enqueue_ns = now; row->queued = true;
    }
    void Progress() {
        while (completed < issued && rows[completed].paced && rows[completed].ended && rows[completed].closed)
            ++completed;
        if (issued == limit && completed == limit) reason = "accepted-limit";
    }
    void Paced(Ticket ticket, uint64_t now) {
        if (Terminal()) return;
        auto* row = Find(ticket); if (!row || failed) return;
        if (!row->queued || row->paced || now < row->enqueue_ns) { Fail("pace-order-or-duplicate"); return; }
        row->paced_ns = now; row->paced = true; Progress();
    }
    bool Start(Ticket ticket, uint64_t position, uint64_t swap, uint32_t words, uint64_t now, bool nested) {
        if (Terminal()) return false;
        auto* row = Find(ticket); if (!row || failed) return false;
        if (!row->queued || row->started || nested || words != kPayloadWords ||
            position != row->queue_position || swap != row->decoder_swap || now < row->enqueue_ns ||
            (ticket.id > 1 && (!rows[ticket.id - 2].ended || now < rows[ticket.id - 2].worker_return_ns))) {
            Fail("worker-identity-or-order"); return false;
        }
        row->worker_ns = now; row->started = true; return true;
    }
    void Close(Ticket ticket, uint64_t frame, uint64_t signal, bool success,
               uint64_t retired, uint64_t native_frame, uint64_t serial, uint64_t native_fence,
               uint64_t native_ns, bool stopped, uint64_t now) {
        if (Terminal()) return;
        auto* row = Find(ticket); if (!row || failed) return;
        if (!row->started || row->closed || !success || !frame || !signal || signal == UINT64_MAX ||
            retired == UINT64_MAX || stopped || now < row->worker_ns ||
            (retired && (!native_frame || !serial || !native_fence || !native_ns || native_frame > frame ||
                        native_fence >= signal || native_ns > now)) ||
            (!retired && (native_frame || serial || native_fence || native_ns))) {
            Fail("close-identity-or-signal"); return;
        }
        if (ticket.id > 1) {
            const auto& previous = rows[ticket.id - 2];
            if (!previous.closed || frame <= previous.frame || signal <= previous.signal || retired < previous.retired ||
                native_frame < previous.native_frame || serial < previous.native_serial || native_fence < previous.native_fence ||
                native_ns < previous.native_ns ||
                (retired == previous.retired && (native_frame != previous.native_frame || serial != previous.native_serial ||
                                                native_fence != previous.native_fence || native_ns != previous.native_ns)) ||
                (retired > previous.retired && (native_frame <= previous.native_frame || serial <= previous.native_serial ||
                                               native_fence <= previous.native_fence || native_ns <= previous.native_ns))) {
                Fail("close-counter-order"); return;
            }
        }
        row->frame = frame; row->signal = signal; row->retired = retired;
        row->native_frame = native_frame; row->native_serial = serial;
        row->native_fence = native_fence; row->native_ns = native_ns;
        row->close_ns = now; row->closed = true;
    }
    void End(Ticket ticket, uint64_t now) {
        if (Terminal()) return;
        auto* row = Find(ticket); if (!row || failed) return;
        if (!row->started || !row->closed || row->ended || now < row->close_ns) { Fail("worker-no-close-or-duplicate"); return; }
        row->worker_return_ns = now; row->ended = true; Progress();
    }
};
inline bool Enabled() {
    static const bool on = [] {
        if (!accepted_retirement::Enabled()) return false;
        const char* value = std::getenv("REGPU_ACCEPTED_RETIRE_THREADED");
        return value && !std::strcmp(value, "1");
    }();
    return on;
}
struct State {
    std::mutex mutex;
    Protocol protocol;
    std::thread::id producer{}, worker{};
    uint64_t native_count = 0, native_frame = 0, native_serial = 0, native_fence = 0, native_ns = 0;
    bool published = false;
    std::string path;
    State() {
        if (const char* p = std::getenv("REGPU_ACCEPTED_RETIRE_OUTPUT")) path = p;
        if (const char* p = std::getenv("FH1_M4_ACCEPTED_SWAP_TRACE_SWAPS")) {
            char* end = nullptr; const auto n = std::strtoul(p, &end, 10);
            protocol.limit = end && !*end && n && n <= accepted_retirement::kMaxRows ? uint32_t(n) : 0;
        }
    }
    void Seal() noexcept {
        if (published || !protocol.Terminal()) return;
        published = true;
        try {
            if (path.empty()) return;
            const std::filesystem::path target(path), partial(path + ".partial");
            std::error_code ec;
            if (std::filesystem::exists(target, ec) || ec || std::filesystem::exists(partial, ec) || ec) return;
            FILE* file = std::fopen(partial.string().c_str(), "w"); if (!file) return;
            bool ok = std::fprintf(file, "#protocol=regpu_accepted_threaded_v1 sealed=1 failed=%u reason=%s limit=%u rows=%u completed=%u\n",
                unsigned(protocol.failed), protocol.reason, protocol.limit, protocol.issued, protocol.completed) > 0;
            ok &= std::fputs("id,generation,decoder_swap,queue_position,decode_ns,enqueue_ns,paced_ns,worker_ns,close_ns,worker_return_ns,frame,signal_fence,retired,native_frame,native_serial,native_fence,native_retire_ns\n", file) >= 0;
            for (uint32_t i = 0; i < protocol.issued; ++i) {
                const auto& r = protocol.rows[i];
                ok &= std::fprintf(file, "%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                    r.ticket.id, (unsigned long long)r.ticket.generation, (unsigned long long)r.decoder_swap,
                    (unsigned long long)r.queue_position, (unsigned long long)r.decode_ns, (unsigned long long)r.enqueue_ns,
                    (unsigned long long)r.paced_ns, (unsigned long long)r.worker_ns, (unsigned long long)r.close_ns,
                    (unsigned long long)r.worker_return_ns, (unsigned long long)r.frame, (unsigned long long)r.signal,
                    (unsigned long long)r.retired, (unsigned long long)r.native_frame, (unsigned long long)r.native_serial,
                    (unsigned long long)r.native_fence, (unsigned long long)r.native_ns) > 0;
            }
            ok &= std::fclose(file) == 0;
            if (ok) std::filesystem::rename(partial, target, ec);
        } catch (...) {}
    }
};
inline State& Get() { static State state; return state; }
inline Ticket& ProducerTicket() { static thread_local Ticket ticket; return ticket; }
inline Ticket& WorkerTicket() { static thread_local Ticket ticket; return ticket; }
inline bool Current() { return bool(WorkerTicket()); }
class ProducerScope {
public:
    ProducerScope() = default;
    ProducerScope(const ProducerScope&) = delete;
    ProducerScope& operator=(const ProducerScope&) = delete;
    void Begin(uint32_t header, uint32_t count, uint32_t vr, uint32_t id, uint64_t swap, bool active) noexcept {
        if (!Enabled()) return;
        try {
            auto& s = Get(); std::lock_guard lock(s.mutex);
            const auto thread = std::this_thread::get_id();
            if ((s.producer != std::thread::id{} && s.producer != thread) || s.worker == thread) {
                s.protocol.Fail("producer-thread-changed"); s.Seal(); return;
            }
            ticket_ = s.protocol.Begin(header, count, vr, id, swap, active, bool(ProducerTicket()), accepted_retirement::Now());
            if (ticket_) { s.producer = thread; ProducerTicket() = ticket_; }
            s.Seal();
        } catch (...) {}
    }
    ~ProducerScope() {
        if (!ticket_) return;
        ProducerTicket() = {};
        try {
            auto& s = Get(); std::lock_guard lock(s.mutex);
            s.protocol.Paced(ticket_, accepted_retirement::Now()); s.Seal();
        } catch (...) {}
    }
private:
    Ticket ticket_{};
};
inline void Bind(Ticket ticket, uint64_t position, uint64_t capacity) noexcept {
    if (!Enabled() || !ticket) return;
    try {
        auto& s = Get(); std::lock_guard lock(s.mutex);
        s.protocol.Bind(ticket, position, capacity, accepted_retirement::Now()); s.Seal();
    } catch (...) {}
}
class WorkerScope {
public:
    WorkerScope(const WorkerScope&) = delete;
    WorkerScope& operator=(const WorkerScope&) = delete;
    WorkerScope(Ticket ticket, uint64_t position, uint64_t swap, uint32_t words) noexcept {
        if (!Enabled()) return;
        try {
            auto& s = Get(); std::lock_guard lock(s.mutex);
            const auto thread = std::this_thread::get_id();
            if ((s.worker != std::thread::id{} && s.worker != thread) || s.producer == thread) {
                s.protocol.Fail("worker-thread-changed"); s.Seal(); return;
            }
            if (s.protocol.Start(ticket, position, swap, words, accepted_retirement::Now(), bool(WorkerTicket()))) {
                s.worker = thread; ticket_ = ticket; WorkerTicket() = ticket;
            }
            s.Seal();
        } catch (...) {}
    }
    ~WorkerScope() {
        if (!ticket_) return;
        WorkerTicket() = {};
        try {
            auto& s = Get(); std::lock_guard lock(s.mutex);
            s.protocol.End(ticket_, accepted_retirement::Now()); s.Seal();
        } catch (...) {}
    }
private:
    Ticket ticket_{};
};
inline void NativeRetired(uint64_t count, uint64_t frame, uint64_t serial, uint64_t fence, bool stopped) noexcept {
    if (!Enabled()) return;
    try {
        auto& s = Get(); std::lock_guard lock(s.mutex);
        if (s.protocol.Terminal()) return;
        const auto thread = std::this_thread::get_id();
        if (stopped || !count || count == UINT64_MAX || !frame || !serial || !fence ||
            count <= s.native_count || frame <= s.native_frame || serial <= s.native_serial || fence <= s.native_fence ||
            (s.worker != std::thread::id{} && s.worker != thread) || s.producer == thread) {
            s.protocol.Fail("native-retirement-identity"); s.Seal(); return;
        }
        s.worker = thread; s.native_count = count; s.native_frame = frame;
        s.native_serial = serial; s.native_fence = fence; s.native_ns = accepted_retirement::Now();
    } catch (...) {}
}
inline void Closed(uint64_t frame, uint64_t signal, bool success, uint64_t retired,
                   uint64_t native_frame, uint64_t serial, uint64_t native_fence, bool stopped) noexcept {
    if (!Enabled() || !Current()) return;
    try {
        auto& s = Get(); std::lock_guard lock(s.mutex);
        if (retired != s.native_count || native_frame != s.native_frame || serial != s.native_serial || native_fence != s.native_fence)
            s.protocol.Fail("native-retirement-receipt-missing");
        else
            s.protocol.Close(WorkerTicket(), frame, signal, success, retired, native_frame, serial, native_fence,
                             s.native_ns, stopped, accepted_retirement::Now());
        s.Seal();
    } catch (...) {}
}
} // namespace regpu::accepted_threaded
