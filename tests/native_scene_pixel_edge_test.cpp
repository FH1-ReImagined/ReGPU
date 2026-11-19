#include "../src/gpu/native_scene_pixel_edge.h"
#include <cassert>
#include <initializer_list>
#include <cmath>
using regpu::native_scene::PixelCenterEdge;
int main(){
 const double oddEdge=360.0*751.0/720.0, evenEdge=360.0*752.0/720.0;
 assert(oddEdge==375.5 && PixelCenterEdge(oddEdge)==375 && std::lround(oddEdge)==376);
 assert(evenEdge==376.0 && PixelCenterEdge(evenEdge)==376);
 for (int height: {751,752}) {
  const double edge=double(height)/2; const int split=PixelCenterEdge(edge),end=PixelCenterEdge(height);
  assert(PixelCenterEdge(0)==0 && end==height);
  for(int y=0;y<height;y++) {
   const bool upperScissor=y<split,lowerScissor=y>=split;
   const bool upperRaster=(double(y)+.5)<edge,lowerRaster=(double(y)+.5)>=edge;
   assert((upperScissor&&upperRaster)!=(lowerScissor&&lowerRaster));
  }
 }
 assert(PixelCenterEdge(256.0*751/720)==267);assert(PixelCenterEdge(512.0*751/720)==534);
 assert(PixelCenterEdge(1280.0*2501/1280)==2501);
}
