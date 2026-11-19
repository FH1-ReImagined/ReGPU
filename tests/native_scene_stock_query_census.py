"""Check the stock census ordering or analyze its retired rows from a ReGPU log."""
import argparse
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
s = (root / "src/gpu/renderer.cpp").read_text()
h = (root / "src/gpu/native_scene_stock_query_census.h").read_text()
start = s.index("void TryPrivateAttachmentPair(")
seed = s[start:s.index("D3D12_DESCRIPTOR_HEAP_DESC sd", start)]
assert "f.census.next={" in seed and seed.index("f.census.next={") < seed.rindex("return;")
assert "if(census)" in seed and "else attempted=true" in seed
assert "PairFootprintsFit" in seed and "StockQueryCensus::InWindow(g_r.frame)" in seed
i = s.index("bool censusQuery=false;")
draw = s[i:s.index("// One isolated replay", i)]
for guard in ("!C().worker", "n.epoch==censusTicket", "g_censusEpoch.load()==censusTicket+1",
              "n.draw==g_r.drawsThisFrame.load()", "n.color==C().color.Get()", "n.depth==C().depth.Get()",
              "g_explicitPhase", "g_msaa==1", "RB_COLOR_MASK", "!g_occlusionOpen", "!coveragePso",
              "!motion", "!vs.info.memExport", "!ps.info.memExport", "!vs.info.usedTextureSlots",
              "!ps.info.usedTextureSlots", "pc->info[1]==0xff", "!(pc->info[2]&64u)", "MapPrivateScissor",
              "g_censusQueries<StockQueryCensus::kMax", "c.next={}"):
    assert guard in draw, guard
ops = ["BeginQuery(", "issue();", "EndQuery(", "ResolveQueryData(", "++g_censusQueries"]
assert [draw.index(x) for x in ops] == sorted(draw.index(x) for x in ops)
assert draw.count("issue();") == 2 and "SetPipelineState" not in draw and draw.count("OMSetRenderTargets")==2 and "if(commonDraw)" in draw
assert "kMax=16" in h and "frame-first<8" in h
assert "completed==UINT64_MAX || completed<fence" in h
assert "censusTicket=StockQueryCensus::Enabled() ? g_censusEpoch.fetch_add(1)" in s
assert s.count("if (StockQueryCensus::Enabled()) ++g_censusEpoch;") == 2
assert "g_r.drawsThisFrame.load()" in s[s.index("auto& n=replay.census.next"):s.index("if (replay.replayArmed)")]
assert s.index("f.census.Read(") < s.index("f.census={};")

def analyze(lines, first):
    totals, rows = {}, []
    for line in lines:
        if "stock query census totals: " in line:
            fields = dict(re.findall(r"(\w+)=(\S+)", line.split("totals: ", 1)[1]))
            f = int(fields["frame"])
            assert f not in totals
            totals[f] = {k:int(v) for k,v in fields.items() if k!="frame"}
        elif "stock query census: " in line:
            fields = dict(re.findall(r"(\w+)=(\S+)", line.split("census: ", 1)[1]))
            for k in ("frame","resolve","draw","fence","completed","readable","passed"):
                fields[k] = int(fields[k])
            assert fields["readable"] in (0,1)
            if fields["readable"]:
                assert 0 < fields["fence"] <= fields["completed"] < 2**64-1
            rows.append(fields)
    assert set(totals)==set(range(first,first+8)), "incomplete eight-frame retirement"
    assert len(rows)<=16 and len({(r["frame"],r["resolve"]) for r in rows})==len(rows)
    for f,t in totals.items():
        assert 0<=t["queries"]<=t["eligible"]<=t["clears"]<=t["ready"]<=t["resolves"]
        assert sum(r["frame"]==f for r in rows)==t["queries"]
    assert all(r["frame"] in totals and 0<r["resolve"]<=totals[r["frame"]]["resolves"] for r in rows)
    return {"frames":totals,"queries":rows,"stock_positive":[r for r in rows if r["readable"] and r["passed"]>0]}

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path, nargs="?")
    parser.add_argument("--first", type=int, default=5043)
    args = parser.parse_args()
    if args.log:
        print(json.dumps(analyze(args.log.read_text(errors="replace").splitlines(),args.first),indent=2))
    else:
        lines = [f"stock query census totals: frame={frame} resolves=1 ready=1 clears=1 eligible=1 queries={int(frame==5043)}"
                 for frame in range(5043,5051)]
        lines.append("stock query census: frame=5043 resolve=1 base=01020304 draw=25 fence=7 completed=8 readable=1 passed=3")
        assert len(analyze(lines,5043)["stock_positive"])==1
        assert not analyze(lines[:-1]+[lines[-1].replace("readable=1","readable=0")],5043)["stock_positive"]
        print("PASS stock-only source guards, bounded query retirement, positive/unknown analyzer controls")
