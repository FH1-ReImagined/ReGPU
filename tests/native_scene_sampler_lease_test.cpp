// Standalone CPU fixture; deliberately not compiled in the source-only handoff.
#include "../src/gpu/native_scene_sampler_lease.h"
#include <cassert>
#include <cstdint>
#include <limits>
using R=regpu::native_scene::SamplerLeaseReceipts<uint32_t>;
int main() {
    uint32_t bank=99;
    assert(R::Bank(4096,4096+3*8*32,32,bank) && bank==3);
    for(uint64_t h:{uint64_t(4095),uint64_t(4097),uint64_t(4096+32*32),UINT64_MAX})
        assert(!R::Bank(4096,h,32,bank));
    assert(!R::Bank(0,0,32,bank)); assert(!R::Bank(4096,4096,0,bank));
    R::Receipt out; R uninitialized; assert(!uninitialized.Take(1,0,out));
    std::array<uint32_t,8> a{1,2,3,4,5,6,7,8}, b{8,7,6,5,4,3,2,1};
    R wrong; wrong.Record(1,0,a); assert(!wrong.Take(2,0,out)); assert(!wrong.Take(1,0,out));
    R good;good.Record(1,0,a); assert(good.Take(1,0,out));const auto immutable=out;
    assert(immutable.descriptors==a && immutable.owner==1 && immutable.serial==1);
    // Normal banks can rotate repeatedly while the separate returned receipt never changes.
    for(unsigned i=0;i<20;++i)good.Record(1,i%4,b);
    assert(immutable.descriptors==a); assert(!good.Take(1,0,out));
    R overflow;overflow.next=std::numeric_limits<uint64_t>::max();overflow.Record(1,0,a);
    assert(overflow.poisoned && !overflow.Take(1,0,out));
    R invalid;invalid.Record(1,4,a);assert(invalid.poisoned && !invalid.Take(1,0,out));
    R zero;zero.Record(0,0,a);assert(!zero.Take(0,0,out));
}
