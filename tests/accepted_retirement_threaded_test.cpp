#include "gpu/accepted_retirement_threaded.h"
#include <cassert>
#include <memory>
using namespace regpu::accepted_threaded;
namespace {
std::unique_ptr<Protocol> Make(uint32_t limit = 2) {
    auto p = std::make_unique<Protocol>(); p->limit = limit; return p;
}
Ticket Emit(Protocol& p, uint32_t id, uint64_t position = 64) {
    auto t = p.Begin(regpu::accepted_retirement::kHeader, 6, 0, id, 100 + id, true, false, id * 100);
    assert(t); p.Bind(t, position, 64, id * 100 + 1); return t;
}
void FinishWorker(Protocol& p, Ticket t, uint64_t position) {
    const auto time = t.id * 100;
    assert(p.Start(t, position, 100 + t.id, kPayloadWords, time + 2, false));
    p.Close(t, 200 + t.id, 300 + t.id, true, t.id, 190 + t.id, 400 + t.id, 290 + t.id, time + 1, false, time + 3);
    p.End(t, time + 4);
}
}
int main() {
    {
        auto p = Make();
        auto a = Emit(*p, 1); p->Paced(a, 103); FinishWorker(*p, a, 64);
        assert(p->completed == 1 && !p->Terminal());
        auto b = Emit(*p, 2, 128); FinishWorker(*p, b, 128);
        assert(!p->Terminal() && p->completed == 1); // worker finished first, CP Pace still running
        p->Paced(b, 210); assert(p->Terminal() && !p->failed && p->completed == 2);
        p->Paced(b, 211); assert(p->completed == 2); // final receipt cannot reopen/reuse
    }
    {
        auto p = Make(); auto a = Emit(*p, 1); p->Paced(a, 103);
        auto b = Emit(*p, 2, 128); p->Paced(b, 203);
        assert(p->Start(a,64,101,6,300,false));
        p->Close(a,201,301,true,1,191,401,291,299,false,303); p->End(a,304);
        assert(p->completed == 1 && !p->Terminal());
        assert(p->Start(b,128,102,6,400,false));
        p->Close(b,202,302,true,2,192,402,292,399,false,403); p->End(b,404);
        assert(p->completed == 2 && p->Terminal()); // CP paced first, later FIFO worker catches up
    }
    {
        auto p = Make(); Emit(*p,1);
        assert(!p->Begin(regpu::accepted_retirement::kHeader,6,0,2,102,true,false,200));
        assert(p->failed); // CP cannot issue next accepted swap before original Pace returned
    }
    {
        auto p = Make(); auto a = Emit(*p,1,0); p->Paced(a,103); FinishWorker(*p,a,0);
        auto b = Emit(*p,2,64); FinishWorker(*p,b,64); p->Paced(b,210);
        assert(p->completed==2 && !p->failed && a.generation!=b.generation); // same physical ring slot, new absolute lifetime
    }
    {
        auto p = Make(1); auto t = Emit(*p, 1, 57); FinishWorker(*p, t, 57); p->Paced(t, 110);
        assert(!p->failed && p->completed == 1); // exact queue end, original pad not needed
    }
    {
        auto p = Make(); auto t = Emit(*p, 1); assert(!p->Start({t.id,t.generation + 1},64,101,6,105,false)); assert(p->failed);
    }
    {
        auto p = Make(); auto t = Emit(*p, 1); assert(!p->Start(t,128,101,6,105,false)); assert(p->failed);
    }
    {
        auto p = Make(); auto t = Emit(*p, 1); assert(!p->Start(t,64,102,6,105,false)); assert(p->failed);
    }
    {
        auto p = Make(); auto t = Emit(*p, 1); assert(!p->Start(t,64,101,3,105,false)); assert(p->failed);
    }
    {
        auto p = Make(); auto t = Emit(*p, 1); assert(!p->Start(t,64,101,6,105,true)); assert(p->failed);
    }
    {
        auto p = Make(); auto t = Emit(*p, 1); assert(p->Start(t,64,101,6,105,false));
        assert(!p->Start(t,64,101,6,106,false)); assert(p->failed);
    }
    {
        auto p = Make(); auto a = Emit(*p, 1); p->Paced(a,103); auto t = Emit(*p, 2, 128);
        assert(!p->Start(t,128,102,6,205,false)); assert(p->failed); // wrong FIFO order
    }
    {
        auto p = Make(); auto t = Emit(*p,1); p->Bind(t,64,64,106); assert(p->failed);
    }
    {
        auto p = Make(); auto t = p->Begin(regpu::accepted_retirement::kHeader,6,0,1,101,true,false,100);
        p->Bind(t,60,64,101); assert(p->failed); // must use actual post-pad absolute head
    }
    {
        auto p = Make(); auto t = Emit(*p,1); p->Paced(t,102); p->Paced(t,103); assert(p->failed);
    }
    {
        auto p = Make(); auto t = Emit(*p,1); assert(p->Start(t,64,101,6,102,false));
        p->End(t,103); assert(p->failed); // missing renderer signal close
    }
    {
        auto p = Make(); auto t = Emit(*p,1); assert(p->Start(t,64,101,6,102,false));
        p->Close(t,201,301,false,0,0,0,0,0,false,103); assert(p->failed); // Signal failure, no stock policy change
    }
    {
        auto p = Make(); auto t = Emit(*p,1); assert(p->Start(t,64,101,6,102,false));
        p->Close(t,201,301,true,0,0,0,0,0,false,103);
        p->Close(t,201,301,true,0,0,0,0,0,false,104); assert(p->failed);
    }
    {
        auto p = Make(); p->generation = UINT64_MAX;
        assert(!p->Begin(regpu::accepted_retirement::kHeader,6,0,1,101,true,false,100)); assert(p->failed);
    }
    for (uint32_t limit : {0u, regpu::accepted_retirement::kMaxRows + 1}) {
        auto p = Make(limit);
        assert(!p->Begin(0,4,0,0,100,true,false,99));
        assert(!p->failed && !p->issued); // ordinary untagged prestart remains a no-op
        assert(!p->Begin(regpu::accepted_retirement::kHeader,6,0,1,101,true,false,100));
        assert(p->failed && p->issued == 0); // tagged invalid capacity cannot alias saturation
    }
    {
        auto p = Make(1); auto t = Emit(*p,1);
        assert(!p->Terminal());
        assert(!p->Begin(0,4,0,0,102,true,false,102));
        assert(!p->Begin(regpu::accepted_retirement::kHeader,6,0,2,102,true,false,103));
        assert(!p->failed && p->issued == 1 && p->completed == 0); // last queued ticket still pending
        FinishWorker(*p,t,64); p->Paced(t,110);
        assert(p->Terminal() && !p->failed && p->completed == 1);
        assert(!p->Begin(regpu::accepted_retirement::kHeader,6,0,2,102,true,false,200));
        assert(p->completed == 1 && p->issued == 1 && !p->failed); // legal exhausted no-op
    }
    {
        auto p = Make(); assert(!p->Begin(0,4,0,0,100,true,false,100)); assert(!p->failed);
        Emit(*p,1); assert(!p->Begin(0,4,0,0,102,true,false,200)); assert(p->failed);
    }
    {
        auto p = Make(); assert(!p->Begin(regpu::accepted_retirement::kHeader,6,1,1,101,true,false,100)); assert(p->failed);
    }
    {
        auto p = Make(); assert(!p->Begin(regpu::accepted_retirement::kHeader,6,0,1,101,false,false,100)); assert(p->failed);
    }
}
