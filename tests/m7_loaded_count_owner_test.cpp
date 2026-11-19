#include "gpu/m7_loaded_count_receipt.h"
#include <cassert>
#include <fstream>
#include <future>
#include <thread>
using namespace regpu::loaded_counts;
int main(int argc,char** argv) {
    assert(argc==3);const std::filesystem::path path(argv[1]);assert(!std::filesystem::exists(path));
    const std::string mode(argv[2]);assert(mode=="wrong-owner" || mode=="reentry");
    _putenv_s("REGPU_M7_COUNT_RECEIPT","1");_putenv_s("REGPU_M7_COUNT_OUTPUT",argv[1]);
    if(mode=="reentry") {
        Begin(100);assert(CanSample());Begin(101);assert(!CanSample());End(1000);End(1001);
    } else {
        std::promise<void> ready,done;auto readyFuture=ready.get_future(),doneFuture=done.get_future();
        std::thread worker([&] {
            Begin(100);assert(CanSample());ready.set_value();doneFuture.wait();
            Closed(200,300,true,0,0,0,0,false,false);End(1000);
        });
        readyFuture.wait();Begin(101);assert(!CanSample());End(1001);done.set_value();worker.join();
    }
    assert(std::filesystem::exists(path));
    std::ifstream input(path);std::string first;std::getline(input,first);
    assert(first.find("failed=1 reason=worker-owner-or-reentry")!=std::string::npos);
}
