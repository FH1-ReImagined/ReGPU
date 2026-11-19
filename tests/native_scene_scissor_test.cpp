#include "../src/gpu/native_scene_scissor.h"
#include <cassert>
using regpu::native_scene::CenteredScissorX;
using regpu::native_scene::MapPrivateScissor;
using regpu::native_scene::ScissorRect;

void TestPrivateScissor() {
 ScissorRect result{};
 assert(MapPrivateScissor({0,0,500,267},{2501,751},{1920,1080},result));
 assert(result.left==0 && result.top==0 && result.right==384 && result.bottom==384);
 assert(MapPrivateScissor({500,267,2501,751},{2501,751},{1920,1080},result));
 assert(result.left==384 && result.top==384 && result.right==1920 && result.bottom==1080);
 assert(MapPrivateScissor({0,0,2501,751},{2501,751},{1920,1080},result));
 assert(result.left==0 && result.top==0 && result.right==1920 && result.bottom==1080);
 assert(!MapPrivateScissor({-1,0,500,267},{2501,751},{1920,1080},result));
 assert(!MapPrivateScissor({0,0,2502,267},{2501,751},{1920,1080},result));
 assert(!MapPrivateScissor({0,0,500,752},{2501,751},{1920,1080},result));
 assert(!MapPrivateScissor({1,0,0,1},{2,2},{2,2},result));
 assert(!MapPrivateScissor({0,0,1,1},{2,2},{0,2},result));
 assert(result.left==0 && result.top==0 && result.right==1920 && result.bottom==1080);
 // Compare against the inverse mapping at every output pixel center, including
 // adjacent splits, odd ratios, exact half-pixel ties and spans that disappear.
 for (uint32_t src=1;src<=12;++src) for (uint32_t dst=1;dst<=12;++dst) {
  for (int32_t first=0; first<int32_t(src); ++first) for (int32_t last=first+1; last<=int32_t(src); ++last) {
   const bool accepted=MapPrivateScissor({first,0,last,1},{src,1},{dst,1},result);
   bool any=false;
   for (int32_t x=0; x<int32_t(dst); ++x) {
    const uint64_t center=uint64_t(2*x+1)*src;
    const bool inside=2ull*first*dst<=center && center<2ull*last*dst;
    assert(inside==(accepted && result.left<=x && x<result.right));
    any|=inside;
   }
   assert(accepted==any);
  }
 }
}

int main(){ TestPrivateScissor(); const double raster=2501.0/1280.0;
 assert(CenteredScissorX(0,1280,raster,1)==0); assert(CenteredScissorX(1280,1280,raster,1)==2501);
 assert(CenteredScissorX(320,1280,raster,1)==625); assert(CenteredScissorX(960,1280,raster,1)==1876);
 const double wide=2560.0*720/(1080.0*1280); const double squeeze=1/wide;
 assert(CenteredScissorX(0,1280,raster,squeeze)==313); assert(CenteredScissorX(1280,1280,raster,squeeze)==2188);
 assert(CenteredScissorX(320,1280,raster,squeeze)==782); assert(CenteredScissorX(960,1280,raster,squeeze)==1719);
 assert(CenteredScissorX(0,1280,raster,1)==0); }
