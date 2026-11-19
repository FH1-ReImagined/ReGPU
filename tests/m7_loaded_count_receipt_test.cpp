#include "gpu/m7_loaded_count_receipt.h"
#include <cassert>
using namespace regpu::loaded_counts;
namespace {
constexpr uint64_t start=1000000000ull, finish=start+60000000000ull;
fh1_m7_window::Snapshot Phase(uint32_t phase=1) { return {1,40,1,start,phase==2 ? finish : 0,phase,1}; }
Frame Sample(unsigned i,bool native=true) {
    return {100+i,200+i,300+i,native ? 10+i : 0,native ? 100+i : 0,native ? 400+i : 0,
            native ? 200+i : 0,true,false,native};
}
void Run(Protocol& p,bool native=true,bool stopProgress=false) {
    p.Observe(Phase(),Sample(0,native),start+10000000,true);
    for (unsigned i=1;i<100;++i) {
        auto f=Sample(i,native);
        if (stopProgress && i>5) { f.count=15;f.native_frame=105;f.serial=405;f.native_fence=205; }
        p.Observe(Phase(),f,start+10000000+i*600000000ull,true);
    }
    auto last=Sample(100,native);
    if (stopProgress) {last.count=15;last.native_frame=105;last.serial=405;last.native_fence=205;}
    p.Observe(Phase(2),last,finish+20000000,true);
}
}
int main() {
    { Protocol p; Run(p); assert(p.sealed && !p.failed && p.observations==101 && p.last.frame.count-p.first.frame.count==100 && p.max_gap<=kProgressGapNs); }
    { Protocol p; Run(p,false); assert(p.sealed && !p.failed && !p.max_count); }
    { Protocol p; Run(p,true,true); assert(p.sealed && p.failed); } // early-only native use cannot qualify
    { Protocol p; p.Observe(Phase(2),Sample(1),finish+1,true); assert(p.failed); }
    { Protocol p; p.Observe(Phase(),Sample(0),start+kEdgeLagNs+1,true); assert(p.failed); }
    { Protocol p; p.Observe(Phase(),Sample(0),start+1,true); p.Observe(Phase(2),Sample(1),finish+kEdgeLagNs+1,true); assert(p.failed); }
    { Protocol p; auto h=Phase(); h.generation=2;p.Observe(Phase(),Sample(0),start+1,true);p.Observe(h,Sample(1),start+2,true);assert(p.failed); }
    { Protocol p; auto h=Phase();h.bytes=39;p.Observe(h,Sample(0),start+1,true);assert(p.failed); }
    { Protocol p; auto h=Phase();h.valid=0;p.Observe(h,Sample(0),start+1,true);assert(p.failed); }
    { Protocol p; auto f=Sample(0);f.stopped=true;p.Observe(Phase(),f,start+1,true);assert(p.failed); }
    { Protocol p; auto f=Sample(0);f.success=false;p.Observe(Phase(),f,start+1,true);assert(p.failed); }
    { Protocol p; auto f=Sample(0);f.signal=UINT64_MAX;p.Observe(Phase(),f,start+1,true);assert(p.failed); }
    { Protocol p; p.Observe(Phase(),Sample(0),start+1,false);assert(p.failed); }
    { Protocol p; auto f=Sample(0);f.requested=false;p.Observe(Phase(),f,start+1,true);assert(p.failed); }
    { Protocol p; p.Observe(Phase(),Sample(0),start+1,true);auto f=Sample(1);f.count=9;p.Observe(Phase(),f,start+2,true);assert(p.failed); }
    { Protocol p; p.Observe(Phase(),Sample(0),start+1,true);p.Observe(Phase(),Sample(0),start+2,true);assert(p.failed); }
    { Protocol p; p.Observe(Phase(),Sample(0),start+1,true);auto h=Phase(0);h.generation=0;h.start_ns=0;p.Observe(h,Sample(1),start+2,true);assert(p.failed); }
    { Protocol p; p.Observe(Phase(),Sample(0),start-1,true);assert(!p.started && !p.failed);p.Observe(Phase(),Sample(1),start+1,true);assert(p.started); } // publication can follow already-loaded now
    {
        Protocol p;p.Observe(Phase(),Sample(0),start+1,true);
        for(unsigned i=1;i<100;++i)p.Observe(Phase(),Sample(i),start+i*600000000ull,true);
        p.Observe(Phase(2),Sample(100),finish-1,true);assert(!p.sealed);
        p.Observe(Phase(2),Sample(101),finish+1,true);assert(p.sealed && !p.failed); // end edge raced loaded clock, deferred without a new clock
    }
}
