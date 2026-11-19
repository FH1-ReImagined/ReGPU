"""Analyze the exact first textured stock query and the final bindless descriptor observations."""
import argparse
import json
import sys
from pathlib import Path
sys.dont_write_bytecode=True
from native_scene_no_clear_census import analyze as analyze_no_clear, fields

root=Path(__file__).resolve().parents[1]
s=(root/"src/gpu/renderer.cpp").read_text()
stock=s[s.index("auto stockPsSlotsAllowed="):s.index("if (!paired) issue();",s.index("auto stockPsSlotsAllowed="))]
audit=stock[:stock.index("if(armedForDraw")]
for term in ("!captureStockTextures","g_textureMutex","pc->tbias[slot][1]","g_r.bindless","entry.resource.Get()","ds.table3.ptr"):
    assert term in audit,term
for term in ("ReadTextureFetch(","GetTexture(","BindlessIndex(","SetGraphics","Create","BeginQuery(","issue();"):
    assert term not in audit,term
private=s[s.index("// One isolated replay of the next actual draw"):]
assert "const bool texturesOk=!vs.info.usedTextureSlots && !ps.info.usedTextureSlots;" in private
assert "stockPsSlotsAllowed" not in private
assert "StockQueryCensus::FirstTextured() && f.census.resolves!=0" in s
assert s.index("stockFetchAddress[slot]=fetch.Address()")<s.index("if (!noSlotMemo && memo.frame")
def analyze(lines):
    result=analyze_no_clear(lines)
    obs=[fields(line,"bindings: ") for line in lines if "stock first textured bindings: " in line]
    slots=[fields(line,"slot: ") for line in lines if "stock first textured slot: " in line]
    assert len(result["dispositions"])<=1 and len(obs)<=1
    if not obs:
        assert not slots and not result["queries"]
    else:
        d=result["dispositions"][0]; h=obs[0]
        assert all(h[k]==d[k] for k in ("frame","resolve","draw"))
        assert d["resolve"]=="1" and d["colorBase"]=="005002D0" and d["depthBase"]=="00000000"
        assert d["vs"]=="5D606C137F9A8DDC" and d["ps"]=="9074C6D4CEDEBB8E"
        vs=int(h["vsSlots"],16); ps=int(h["psSlots"],16); assert vs==0 and ps>0
        assert sorted(int(row["slot"]) for row in slots)==[i for i in range(32) if ps&(1<<i)]
        for row in slots:
            assert all(row[k]==d[k] for k in ("frame","resolve","draw"))
            idx=int(row["index"]); ptr=int(row["resource"],16)
            assert row["state"]==("null" if idx<3 else "known" if ptr else "unknown")
    result.update(bindings=obs,slots=slots)
    return result
if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("log",type=Path,nargs="?");a=p.parse_args()
    if a.log: print(json.dumps(analyze(a.log.read_text(errors="replace").splitlines()),indent=2))
    else:
        lines=[
            "stock query census totals: frame=5043 resolves=1 ready=1 clears=0 eligible=1 queries=1",
            "stock no-clear disposition: frame=5043 resolve=1 base=1CE2D000 control=00100000 draw=21 colorBase=005002D0 depthBase=00000000 vs=5D606C137F9A8DDC ps=9074C6D4CEDEBB8E ready=1 status=queried",
            "stock query census: frame=5043 resolve=1 base=1CE2D000 draw=21 colorBase=005002D0 depthBase=00000000 vs=5D606C137F9A8DDC ps=9074C6D4CEDEBB8E fence=9 completed=10 readable=1 passed=7",
            "stock first textured bindings: frame=5043 resolve=1 draw=21 vsSlots=00000000 psSlots=00000002",
            "stock first textured slot: frame=5043 resolve=1 draw=21 slot=1 index=10 resource=AAA state=known"]
        assert len(analyze(lines)["stock_positive"])==1
        assert not analyze([r.replace("passed=7","passed=0") for r in lines])["stock_positive"]
        print("PASS first textured stock identity, slot/retirement joins and private guard isolation")
