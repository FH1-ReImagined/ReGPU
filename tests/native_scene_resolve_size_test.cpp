#include "../src/gpu/native_scene_resolve_size.h"
#include <cassert>
using namespace regpu::native_scene;
int main(){
 auto main=SelectResolveSizing(false,true,true,1280,720,1280,720,1);assert(main==ResolveSizing::ExactMain);
 assert(ResolveDimension(main,1280,2501.0/1280,2501,2501)==2501);assert(ResolveDimension(main,720,751.0/720,751,751)==751);
 auto small=SelectResolveSizing(false,true,false,32,1,1280,720,1);assert(small==ResolveSizing::Floor);
 assert(ResolveDimension(small,1,1.5,2,751)==1);assert(ResolveDimension(small,32,1.5,48,2501)==48);
 assert(ResolveDimension(small,12,751.0/720,13,751)==12);assert(ResolveDimension(small,13,751.0/720,14,751)==13);
 assert(SelectResolveSizing(true,true,true,1280,720,1280,720,1)==ResolveSizing::Round);
 assert(ResolveDimension(ResolveSizing::Round,1,1.5,2,0)==2);
 assert(SelectResolveSizing(false,false,true,1280,720,1280,720,1)==ResolveSizing::Floor);
 assert(SelectResolveSizing(false,true,true,1280,720,1280,1024,1)==ResolveSizing::Floor);
 assert(SelectResolveSizing(false,true,true,1280,720,1280,720,0.5f)==ResolveSizing::Floor);
 assert(SelectResolveSizing(false,true,false,1280,720,1280,720,1)==ResolveSizing::Floor);
}
