"""Verify frame-5043 no-clear selectors and report both retired GPU queries and refusals."""
import argparse
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
s = (root / "src/gpu/renderer.cpp").read_text()
h = (root / "src/gpu/native_scene_stock_query_census.h").read_text()
select = s[s.index("void TryPrivateAttachmentPair("):s.index("D3D12_DESCRIPTOR_HEAP_DESC sd",s.index("void TryPrivateAttachmentPair("))]
for term in ("base!=0x1CE2D000u || depth", "control!=0x00100000u", "rect.right!=LONG(kWidth)",
             "f.census.resolves>=StockQueryCensus::kNoClearMax","!recorded","explicitCoverage.Complete",
             "depthBase","PairFootprintsFit","f.census.next={", "C().color.Get(),C().depth.Get(),false"):
    assert term in select, term
assert 'return first==5043 && frame==5043' in h and 'kNoClearMax=2' in h
assert 'Finish("intervening_rebind")' in s and 'Finish("intervening_resolve")' in s
assert 'owner->active==ordinal' in h and 'next_draw_not_issued' in h
assert 'c.active && n.ordinal==c.active) : n.cleared' in s
assert s.index('Finish("queried")') > s.index('ResolveQueryData(c.heap.Get()')
assert 'if(!active) return;' in h and h.index('dispositions[active-1].first=reason') < h.index('active=0;',h.index('dispositions[active-1].first=reason'))
def fields(line, tag):
    return dict(re.findall(r"(\w+)=(\S+)",line.split(tag,1)[1]))
def analyze(lines):
    totals=[]; dispositions={}; queries={}
    for line in lines:
        if "stock query census totals: " in line:
            totals.append(fields(line,"totals: "))
        elif "stock no-clear disposition: " in line:
            d=fields(line,"disposition: "); idx=int(d["resolve"])
            assert idx not in dispositions and d["frame"]=="5043" and d["base"]=="1CE2D000" and d["control"]=="00100000"
            assert d["status"]!="unclassified" and d["ready"] in ("0","1")
            dispositions[idx]=d
        elif "stock query census: " in line:
            q=fields(line,"census: "); idx=int(q["resolve"])
            assert idx not in queries and q["frame"]=="5043" and q["base"]=="1CE2D000"
            assert q["readable"] in ("0","1")
            if q["readable"]=="1": assert 0<int(q["fence"])<=int(q["completed"])<2**64-1
            queries[idx]=q
    assert len(totals)==1 and totals[0]["frame"]=="5043"
    t=totals[0]; assert int(t["clears"])==0
    assert 0<=len(dispositions)==int(t["resolves"])<=2 and set(dispositions)==set(range(1,len(dispositions)+1))
    assert len(queries)==int(t["queries"]) and sum(d["ready"]=="1" for d in dispositions.values())==int(t["ready"])
    assert set(queries)=={i for i,d in dispositions.items() if d["status"]=="queried"}
    for i,q in queries.items():
        d=dispositions[i]
        assert d["ready"]=="1" and all(q[key]==d[key] for key in ("draw","colorBase","depthBase","vs","ps"))
    return {"totals":t,"dispositions":list(dispositions.values()),"queries":list(queries.values()),
            "stock_positive":[q for q in queries.values() if q["readable"]=="1" and int(q["passed"])>0]}
if __name__=="__main__":
    parser=argparse.ArgumentParser();parser.add_argument("log",type=Path,nargs="?");args=parser.parse_args()
    if args.log:
        print(json.dumps(analyze(args.log.read_text(errors="replace").splitlines()),indent=2))
    else:
        sample=["stock query census totals: frame=5043 resolves=2 ready=1 clears=0 eligible=0 queries=0",
                "stock no-clear disposition: frame=5043 resolve=1 base=1CE2D000 control=00100000 ready=1 status=intervening_resolve",
                "stock no-clear disposition: frame=5043 resolve=2 base=1CE2D000 control=00100000 ready=0 status=coverage"]
        assert not analyze(sample)["stock_positive"]
        print("PASS exact no-clear occurrence, independent depth, expiry and disposition controls")
