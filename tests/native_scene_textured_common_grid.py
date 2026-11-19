"""Validate the one-shot capture's copy boundaries or analyze a retired common-grid witness."""
import argparse
import json
import re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
h=(root/"src/gpu/native_scene_textured_common_grid.h").read_text()
s=(root/"src/gpu/renderer.cpp").read_text()
assert h.count("CopyTextureRegion(&dst,0,0,0,&src,nullptr)")==3 and "D3D12_BOX" not in h
assert "p.logicalFootprint.Footprint.RowPitch=p.full.Footprint.RowPitch" in h and "p.logicalFootprint.Offset=p.full.Offset" in h
assert "limit-used" in h and "GetResourceAllocationInfo" in h and "stride>limit/4" in h
capture=s[s.index("bool censusQuery=false;"):s.index("// One isolated replay of the next actual draw",s.index("bool censusQuery=false;"))]
for token in ("rt->second.resource.Get()!=grid.primary.Get()","rt->second.serial!=grid.serial","grid.epoch!=g_resolveEpoch",
              "g_bindingsEpoch!=grid.epoch","ps.info.usedTextureSlots!=2","stockFetchAddress[1]!=n.base","LeanFetch()",
              "e->second.lastFrame!=g_r.frame","ds.table3.ptr!=Gpu(","e->second.index!=grid.descriptor",
              "d.MipLevels!=1","d.DepthOrArraySize!=1","grid.primary.Get()==n.depth","RendererDrawWorkers()"):
    assert token in capture,token
seq=["grid.Seed(*C().list)","grid.samples.heap.Get(),D3D12_QUERY_TYPE_OCCLUSION,0);","if (!paired) issue();",
     "EndQuery(grid.samples.heap.Get(),D3D12_QUERY_TYPE_OCCLUSION,0)","OMSetRenderTargets(1,&rtv",
     "BeginQuery(grid.samples.heap.Get(),D3D12_QUERY_TYPE_OCCLUSION,1)","issue();","EndQuery(grid.samples.heap.Get(),D3D12_QUERY_TYPE_OCCLUSION,1)",
     "OMSetRenderTargets(1,&C().rtv","grid.CaptureStock(*C().list,2)","grid.CapturePrivate(*C().list,3)"]
i=0
for token in seq:
    i=capture.index(token,i)+len(token)
assert "BindlessIndex(" not in capture and "SetDescriptorHeaps(" not in capture and "SetPipelineState(" not in capture
assert "f.common.recorded && f.common.descriptor==it->second.index" in s
swap=s[s.index("const UINT64 submittedRendererFrame=g_r.frame;"):]
assert swap.index("++g_r.frame;")<swap.index("Signal(g_r.fence.Get(), Frame().fence)")<swap.index("Frame().common.frame==submittedRendererFrame")
assert "if(SUCCEEDED(frameSignal)) Frame().common.fence=Frame().fence;" in swap

def data(line,tag):
    return dict(re.findall(r"(\w+)=(\S+)",line.split(tag,1)[1]))
def analyze(lines):
    planes=[data(l,"plane: ") for l in lines if "textured common plane: " in l]
    result=[data(l,"result: ") for l in lines if "textured common result: " in l]
    disp=[data(l,"disposition: ") for l in lines if "stock no-clear disposition: " in l]
    assert len(disp)<=1 and len(result)<=1
    if disp: assert disp[0]["frame"]=="5043" and disp[0]["resolve"]=="1" and disp[0]["base"]=="1CE2D000"
    if not result:
        assert not planes and (not disp or disp[0]["status"]!="common_recorded"),"missing retirement"
        return {"dispositions":disp,"accepted":False}
    r=result[0]
    assert len(disp)==1 and disp[0]["status"]=="common_recorded" and disp[0]["draw"]==r["draw"] and r["frame"]=="5043"
    if "fenceSignalFailed" in r or "deviceRemoved" in r:
        assert not planes and r["accepted"]=="0"
        return {"result":r,"accepted":False}
    assert 0<int(r["fence"])<=int(r["completed"])<2**64-1 and len(planes)==3
    assert {int(p["plane"]) for p in planes}=={0,1,2}
    for p in planes:
        assert p["frame"]==r["frame"] and p["draw"]==r["draw"] and int(p["pixels"])==2501*751 and p["readable"]==r["readable"]
        assert all(0<=int(p[k])<=2501*751 for k in ("seedMismatch","postMismatch","stockChanged"))
    accepted=(r["readable"]=="1" and int(r["stockPassed"])>0 and r["stockPassed"]==r["privatePassed"]
              and all(int(p["seedMismatch"])==int(p["postMismatch"])==0 for p in planes)
              and any(int(p["stockChanged"])>0 for p in planes))
    assert accepted==(r["accepted"]=="1")
    return {"result":r,"planes":planes,"accepted":accepted}

if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("log",type=Path,nargs="?");a=p.parse_args()
    if a.log: print(json.dumps(analyze(a.log.read_text(errors="replace").splitlines()),indent=2))
    else:
        rows=["stock no-clear disposition: frame=5043 draw=70 resolve=1 base=1CE2D000 status=common_recorded",
              "textured common result: frame=5043 draw=70 fence=7 completed=8 readable=1 stockPassed=94 privatePassed=94 accepted=1"]
        rows += [f"textured common plane: frame=5043 draw=70 plane={i} pixels={2501*751} readable=1 seedMismatch=0 postMismatch=0 stockChanged={int(i==0)}" for i in range(3)]
        assert analyze(rows)["accepted"]
        assert not analyze([r.replace("accepted=1","accepted=0").replace("postMismatch=0","postMismatch=1") for r in rows])["accepted"]
        assert not analyze([r.replace("accepted=1","accepted=0").replace("stockChanged=1","stockChanged=0") for r in rows])["accepted"]
        print("PASS whole-plane seed, source lifetime, stock/private issue order, real fence and result controls")
