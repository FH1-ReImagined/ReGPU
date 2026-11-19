from pathlib import Path
r=Path(__file__).resolve().parents[1]
s=(r/'src/gpu/renderer.cpp').read_text()
assert 'Frame().replayDrawArmed=false; // A rebinding' in s
assert 'Frame().replayDrawArmed=false; // Any intervening resolve' in s
start=s.index('// One isolated replay of the next actual draw'); end=s.index('if (g_perfTiming)',start);block=s[start:end]
for test in ('replay.replayDrawArmed=false','replay.replayFrame==g_r.frame','replay.replayDraw==g_r.drawsThisFrame.load()','replay.replaySourceColor.Get()==C().color.Get()','replay.replaySourceDepth.Get()==C().depth.Get()','!g_occlusionOpen','!coveragePso','!motion','!vs.info.memExport','!ps.info.memExport','!vs.info.usedTextureSlots','!ps.info.usedTextureSlots','pc->info[1]==0xff','!(pc->info[2]&64u)','MapPrivateScissor(', 'viewportOk && scissorOk', 'const D3D12_RECT outScissor{mappedScissor.left,mappedScissor.top,mappedScissor.right,mappedScissor.bottom}','replay.privatePairReadback.bytes'):
 assert test in block,test
assert block.index('OMSetRenderTargets(1,&rtv,FALSE,&dsv)') < block.index('issue();',block.index('RSSetScissorRects(1,&outScissor)')) < block.index('OMSetRenderTargets(1,&C().rtv,FALSE,&C().dsv)')
assert 'RSSetViewports(1,&vp)' in block and 'RSSetScissorRects(1,&hostScissor)' in block
assert block.index('replay.privateDrawColorBefore.Record(') < block.index('OMSetRenderTargets(1,&rtv,FALSE,&dsv)')
assert block.index('issue();',block.index('RSSetScissorRects(1,&outScissor)')) < block.index('replay.privatePairClearReadback.Record(') < block.index('pairedAfter =')
assert 'PrivatePairReadback::kMaxBytes - firstBytes * 2' in block
assert 'PrivatePairReadback::kMaxBytes-replay.privatePairReadback.bytes-replay.privateDrawColorBefore.bytes' in block
assert s.index('before.Compare(f.privatePairClearReadback)') < s.index('f.privatePairClearReadback.Finish()')
assert 'complete != UINT64_MAX && f.fence && complete >= f.fence' in s
print('PASS isolated next-draw replay guards and restoration; no GPU execution')

# Paired mode measures both complete four-image sets around exactly one stock issue.
draw=s[s.index('const bool paired=armed'):end]
pre=draw.index('beforeReady=replay.privatePairReadback.Record')
stock=draw.index('issue();',pre)
private=draw.index('issue();',draw.index('RSSetScissorRects(1,&outScissor)',stock))
post=draw.index('queued=replay.privatePairClearReadback.Record',private)
assert draw.index('if (!paired) issue();') < pre < stock < private < post
assert 'if(valid && (!paired || beforeReady))' in draw
assert 'PrivatePairReadback::kMaxBytes/2' in draw and 'if (!paired && delta' in draw
assert s.index('PrivatePairedDelta::Compare(') < s.index('f.privatePairReadback.Finish()')
assert 'f.fence && completed!=UINT64_MAX && completed>=f.fence' in s
assert 'defer && slotsAvailable' in s and 'if (!defer)' in s
print('PASS paired stock/private ordering, budget, closed failure and retirement')

# Non-binary, diagnostic-only queries tightly bound each of the two normal draw calls.
def q(op, slot):
    return draw.index(f'{op}(replay.privateSamples.heap.Get(),D3D12_QUERY_TYPE_OCCLUSION,{slot})')
resolve=draw.index('ResolveQueryData(replay.privateSamples.heap.Get(),D3D12_QUERY_TYPE_OCCLUSION,0,2')
assert pre < q('BeginQuery',0) < stock < q('EndQuery',0) < q('BeginQuery',1) < private < q('EndQuery',1) < resolve < post
assert draw.count('issue();')==4  # ordinary stock, common-grid private, paired stock, prior private
assert 'beforeReady && replay.privateSamples.requested' in draw
assert '(countSamples ? PrivatePassedSamples::kBytes : 0)' in draw
assert 'SetPipelineState' not in draw and 'g_occlusionHeap' not in draw
helper=(r/'src/gpu/native_scene_private_samples.h').read_text()
assert 'D3D12_QUERY_HEAP_TYPE_OCCLUSION' in helper and 'q.Count=2' in helper
assert 'D3D12_RESOURCE_STATE_COPY_DEST' in helper
assert 'completed==UINT64_MAX || completed<fence' in helper
print('PASS independent sample-query placement, paired retirement, budget and fallback')
