#include "gpu/accepted_retirement_threaded.h"
#include <cassert>
#include <fstream>
#include <future>
using namespace regpu::accepted_threaded;
int main(int argc, char** argv) {
    assert(argc == 3);
    const std::filesystem::path path(argv[1]);
    const std::string mode(argv[2]);
    assert(mode == "worker-first" || mode == "producer-first" || mode == "wrong-owner");
    assert(!std::filesystem::exists(path) && !std::filesystem::exists(path.string() + ".partial"));
    _putenv_s("REGPU_ACCEPTED_RETIRE_TRACE", "1");
    _putenv_s("REGPU_ACCEPTED_RETIRE_THREADED", "1");
    _putenv_s("FH1_M4_ACCEPTED_SWAP_TRACE_SWAPS", "1");
    _putenv_s("REGPU_ACCEPTED_RETIRE_OUTPUT", argv[1]);
    std::promise<void> ready; auto future = ready.get_future();
    std::thread worker;
    {
        ProducerScope producer;
        producer.Begin(regpu::accepted_retirement::kHeader,6,0,1,101,true);
        const auto ticket = ProducerTicket(); assert(ticket);
        Bind(ticket,64,64);
        worker = std::thread([&,ticket] {
            future.wait();
            WorkerScope consume(ticket,64,101,6);
            assert(Current());
            NativeRetired(1,190,400,290,false);
            Closed(201,301,true,1,190,400,290,false);
        });
        if (mode != "producer-first") {
            ready.set_value(); worker.join();
            assert(!std::filesystem::exists(path)); // worker completed, producer Pace still open
            if (mode == "wrong-owner")
                NativeRetired(2,191,401,291,false); // producer cannot become result owner
        }
    }
    if (mode == "producer-first") {
        assert(!std::filesystem::exists(path)); // producer completed, queued worker still pending
        ready.set_value(); worker.join();
    }
    assert(std::filesystem::exists(path) && !std::filesystem::exists(path.string() + ".partial"));
    std::ifstream input(path); std::string first; std::getline(input,first);
    assert(first.find(mode == "wrong-owner" ? "failed=1 reason=native-retirement-identity" : "failed=0 reason=accepted-limit") != std::string::npos);
    if (mode != "wrong-owner") {
        auto& state = Get(); std::lock_guard lock(state.mutex);
        assert(state.protocol.completed == 1 && state.protocol.rows[0].paced && state.protocol.rows[0].ended);
        assert(state.producer != state.worker && state.protocol.rows[0].ticket.generation == 1);
    }
}
