#include "gpu/m7_loaded_count_receipt.h"
#include <cassert>
#include <fstream>
#include <memory>
using namespace regpu::loaded_counts;
int main(int argc,char** argv) {
    assert(argc==2);const std::filesystem::path path(argv[1]);
    assert(!std::filesystem::exists(path) && !std::filesystem::exists(path.string()+".partial"));
    { auto s=std::make_unique<State>();s->path=path.string(); }
    assert(!std::filesystem::exists(path)); // destructor never publishes
    {
        auto s=std::make_unique<State>();s->path=path.string();s->owner.store(GetCurrentThreadId());
        fh1_m7_window::Snapshot h{1,40,1,1000000000,0,1,1};
        for(unsigned i=0;i<100;++i) {
            Frame f{100+i,200+i,300+i,0,0,0,0,true,false,false};
            s->protocol.Observe(h,f,1000000001ull+i*600000000ull,true);
        }
        s->Seal();assert(!std::filesystem::exists(path)); // missing real host end cannot publish success
        h.phase=2;h.end_ns=61000000000ull;
        s->protocol.Observe(h,Frame{200,300,400,0,0,0,0,true,false,false},61000000001ull,true);
        assert(s->protocol.sealed && !s->protocol.failed);s->Seal();
        assert(std::filesystem::exists(path) && !std::filesystem::exists(path.string()+".partial"));
    }
    std::ifstream input(path);const std::string before((std::istreambuf_iterator<char>(input)),{});input.close();
    assert(before.find("failed=0 reason=completed-window")!=std::string::npos);
    { auto s=std::make_unique<State>();s->path=path.string();s->protocol.Fail("fixture");s->Seal(); }
    std::ifstream again(path);const std::string after((std::istreambuf_iterator<char>(again)),{});
    assert(before==after);
}
