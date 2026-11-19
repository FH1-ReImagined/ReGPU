"""Executed source/ordering controls; C++ receipt fixture remains uncompiled."""
from pathlib import Path
r=Path(__file__).resolve().parents[1];s=(r/'src/gpu/renderer.cpp').read_text();h=(r/'src/gpu/native_scene_sampler_lease.h').read_text()
def scope(a,b):return s[s.index(a):s.index(b,s.index(a))]
flag=scope('bool CommonSamplerLeaseRequested()', 'using CommonSamplerReceipts=')
assert 'std::strcmp(v,"1")==0' in flag and 'StockQueryCensus::CommonGrid()' in flag
writer=scope('void WriteSamplerBank(', '// The sampler table for the next draw')
assert 'bank>=kSamplerBanks' in writer and 'if(CommonSamplerLeaseRequested()) receipt[i]=s;' in writer
assert writer.index('CreateSampler(&s, h)')<writer.index('g_commonSamplerReceipts.Record(')
assert 'const UINT bank = (g_samplerBank.load() + 1) % kSamplerBanks;' in s
prepare=scope('bool PrepareCommonSamplerLease(', 'std::atomic<uint64_t> g_outputRequest')
for token in ['GetDevice(IID_PPV_ARGS(&owner))','owner.Get()!=g_r.device','std::lock_guard lock(g_samplerMutex)',
              'bank!=g_samplerBank.load()', 'CommonSamplerReceipts::capacity','CommonSamplerReceipts::reserved',
              'grid.samplerHeap=g_r.samplerHeap','grid.samplerOriginal=actual','grid.samplerLease=true']:
    assert token in prepare,token
assert prepare.count('GetDeviceRemovedReason()')==2
assert 'CopyDescriptors' not in prepare and 'SetDescriptorHeaps' not in prepare
assert prepare.index('g_commonSamplerReceipts.Take(')<prepare.index('CreateSampler(')<prepare.index('grid.samplerLease=true')
capture=scope('bool censusQuery=false;', '// One isolated replay of the next actual draw')
assert '(!dynamicLease && (LeanFetch() || g_r.samplerHeap))' in capture
for t in ['RendererDrawWorkers()', 'g_occlusionOpen || coveragePso', 'ps.info.usedTextureSlots!=2',
          'ds.pso!=pso','ds.motionRoot || ds.motionTargets','ds.table3.ptr!=Gpu(']:assert t in capture,t
order=['grid.Prepare(g_r.device,n.color,n.depth)','PrepareCommonSamplerLease(grid,ds.samplers)',
       'SetGraphicsRootDescriptorTable(4,grid.samplerImmutable)','ds.samplers=grid.samplerImmutable',
       'grid.Seed(*C().list)','if (!paired) issue();','OMSetRenderTargets(1,&rtv','issue();',
       'OMSetRenderTargets(1,&C().rtv','grid.recorded=true;',
       'SetGraphicsRootDescriptorTable(4,grid.samplerOriginal)','ds.samplers=grid.samplerOriginal']
pos=0
for t in order:pos=capture.index(t,pos)+len(t)
assert 'SetDescriptorHeaps(' not in capture
retire=scope('if(StockQueryCensus::CommonGrid() && f.common.recorded', 'if (f.census.frame)')
assert retire.index('completed>=f.common.fence')<retire.index('common sampler lease retired:')<retire.index('f.common={};')
assert 'completed==UINT64_MAX' in retire and 'f.common.signalFailed=true' in retire
for t in ['reserved=32, capacity=40','attempted=true;', 'if(attempted) return false;', 'next==std::numeric_limits<uint64_t>::max()', 'receipts[bank].owner!=owner']:assert t in h,t
print('PASS strict gate, actual CPU receipt, reserved range, device identity, both draw bindings/restoration, default exclusion, real-fence retirement source controls')
