from pathlib import Path
import math
r=Path(__file__).resolve().parents[1]
h=(r/'src/gpu/native_scene_private_pair_readback.h').read_text()
c=(r/'src/gpu/renderer.cpp').read_text()
assert 'kMaxBytes=256ull*1024*1024' in h
assert h.index('CreateCommittedResource') < h.index('ResourceBarrier(1,&b)')
assert 'D3D12_RESOURCE_STATE_COPY_SOURCE' in h and 'b.Transition.StateAfter=states[i]' in h
assert 'D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX' in h and 'CopyTextureRegion(&dst,0,0,0,&src,nullptr)' in h
assert 'complete!=UINT64_MAX && complete>=f.fence' in c
assert c.index('f.privatePairReadback.Finish()') < c.index('f.garbage.clear()')
assert 'std::strcmp(readbackGate,"1")==0' in c
for sw,sh,dw,dh in [(2501,751,1920,1080),(2501,752,1920,1080),(2,4,4,2),(1920,1080,1920,1080)]:
 for iy in range(16):
  for ix in range(16):
   x=ix*(dw-1)//15;y=iy*(dh-1)//15
   sx=int((x+.5)*sw/dw);sy=int((y+.5)*sh/dh)
   assert 0<=sx<sw and 0<=sy<sh
   px=min(max((x+.5)*sw/dw-.5,0),sw-1);py=min(max((y+.5)*sh/dh-.5,0),sh-1)
   assert 0<=math.floor(px)<sw and 0<=math.floor(py)<sh
print('PASS bounded readback source and sampling geometry; no GPU execution')
