#include "../src/gpu/native_scene_alias_scale.h"
#include <cassert>
using namespace regpu::native_scene;
int main() { AlternateViewEvidence e{true,true,true,true,true,true,640,2,8,0,0x00500000,0x01400000,0x005002d0,0x01400b40,0,0x20002000,0,640,-360,640,360};
 assert(IsKnownPrimaryRectangle(e)); auto n=e;n.explicitPhase=false;assert(!IsKnownPrimaryRectangle(n));n=e;n.pitch=1280;n.mode=0;assert(!IsKnownPrimaryRectangle(n));n=e;n.output16x9=false;assert(!IsKnownPrimaryRectangle(n));n=e;n.depthNative^=4;assert(!IsKnownPrimaryRectangle(n));n=e;n.colorNative|=2;assert(!IsKnownPrimaryRectangle(n));n=e;n.colorMask=15;assert(!IsKnownPrimaryRectangle(n));n=e;n.depthOnly=false;assert(!IsKnownPrimaryRectangle(n));n=e;n.textureless=false;assert(!IsKnownPrimaryRectangle(n));n=e;n.scissorBR=0x01680280;assert(!IsKnownPrimaryRectangle(n));n=e;n.msaaScaleEnabled=false;assert(!IsKnownPrimaryRectangle(n));n=e;n.vx=320;assert(!IsKnownPrimaryRectangle(n));
 // Shader retains the original 2x view multiplier; host X then uses 2501/1280.
 const double view=2.0, x=640.0, wide=2501.0/1280.0, y=751.0/720.0;assert(x*view*wide==2501.0);assert(x*view*y!=2501.0);
}
