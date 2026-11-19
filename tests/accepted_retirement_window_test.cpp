#include "gpu/accepted_retirement_window.h"
#include <cassert>
using namespace regpu::accepted_retirement;
Protocol Ready() { Protocol p; p.limit=3; return p; }
void Close(Protocol& p, uint64_t f, uint64_t n) {
    p.Close(f, f+100, true, n, n?f-1:0, n?n+1:0, n?f+90:0, false);
}
int main() {
    // Exact policy used by the actual decoder and queue-signal hooks; no GPU call is mocked as real evidence.
    auto p=Ready();
    assert(!p.Begin(0xC0036400,4,0,0,1,false,false) && !p.failed && !p.rows);
    for(uint32_t id=1;id<=3;++id) {
        assert(p.Begin(kHeader,6,0,id,10+id,false,false));
        Close(p,100+id,id);
        assert(p.End());
    }
    assert(p.sealed && !p.failed && p.rows==3);
    auto old=p.previous;
    assert(!p.Begin(kHeader,6,0,1,99,false,false) && p.previous.id==old.id);
    for(unsigned which=0;which<7;++which) {
        auto q=Ready();
        bool result=q.Begin(which==0?0xC0046400:kHeader,which==1?5:6,which==2?7:0,
                            which==3?0:which==4?4:1,20,which==5,which==6);
        assert(!result);
        // A VR packet before an accepted tag is deliberately ignored, never treated as tagged coverage.
        if(which==0) assert(!q.rows && !q.active); else assert(q.failed && q.sealed);
    }
    auto missing=Ready(); assert(missing.Begin(kHeader,6,0,1,20,false,false));assert(!missing.End() && missing.failed);
    auto duplicate=Ready();assert(duplicate.Begin(kHeader,6,0,1,20,false,false));Close(duplicate,100,1);assert(duplicate.End());
    assert(!duplicate.Begin(kHeader,6,0,1,21,false,false) && duplicate.failed);
    auto gap=Ready();assert(!gap.Begin(kHeader,6,0,2,20,false,false) && gap.failed);
    auto interrupted=Ready();assert(interrupted.Begin(kHeader,6,0,1,20,false,false));Close(interrupted,100,1);assert(interrupted.End());
    assert(!interrupted.Begin(0xC0036400,4,0,0,21,false,false) && interrupted.failed);
    for(unsigned which=0;which<5;++which) {
        auto q=Ready();assert(q.Begin(kHeader,6,0,1,20,false,false));
        q.Close(100,200,which!=0,which==1?UINT64_MAX:1,99,2,199,which==2);
        if(which==3) q.Close(100,200,true,1,99,2,199,false);
        if(which==4) { assert(q.End());assert(q.Begin(kHeader,6,0,2,21,false,false));q.Close(101,201,true,0,0,0,0,false); }
        assert(q.failed && q.sealed);
    }
    auto reset=Ready();assert(reset.Begin(kHeader,6,0,1,20,false,false));Close(reset,100,1);assert(reset.End());
    assert(!reset.Begin(kHeader,6,0,2,20,false,false) && reset.failed);
    auto invalid_limit=Ready();invalid_limit.limit=kMaxRows+1;assert(!invalid_limit.Begin(kHeader,6,0,1,20,false,false) && invalid_limit.failed);
}
