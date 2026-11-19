#include "../src/gpu/native_scene_resolve_coverage.h"
#include <cassert>
using regpu::native_scene::ResolveCoverage;
int main(){ ResolveCoverage c; auto r=[&](unsigned t,unsigned b){return c.Record(1,1,10,100,100,0,t,100,b,true);};
 assert(r(60,100));assert(r(0,30));assert(!c.Complete(1,1,10,100,100));assert(r(20,60));assert(c.Complete(1,1,10,100,100));assert(r(20,60));assert(c.Complete(1,1,10,100,100));
 assert(!c.Record(1,1,10,100,100,0,0,100,30,false));assert(!c.Complete(1,1,10,100,100));assert(r(0,100));assert(!c.Complete(2,1,10,100,100));assert(!c.Complete(1,2,10,100,100));
 assert(c.Record(2,1,10,100,100,0,0,100,40,true));assert(!c.Complete(1,1,10,100,100));assert(!c.Record(2,1,10,100,100,1,40,100,100,true));assert(!c.Complete(2,1,10,100,100)); }
