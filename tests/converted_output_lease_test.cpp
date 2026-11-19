#include "gpu/native_scene_converted_output.h"
#include "gpu/native_scene_sampler_lease.h"
#include <array>
#include <cstring>

int main() {
    using Receipts=regpu::native_scene::SamplerLeaseReceipts<D3D12_SAMPLER_DESC>;
    std::array<D3D12_SAMPLER_DESC,8> original{};
    for (UINT i=0;i<8;++i) {
        auto& s=original[i];
        s.Filter=i%2 ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU=D3D12_TEXTURE_ADDRESS_MODE(i%5+1);
        s.AddressV=D3D12_TEXTURE_ADDRESS_MODE((i+1)%5+1);
        s.AddressW=D3D12_TEXTURE_ADDRESS_MODE((i+2)%5+1);
        s.MipLODBias=float(i)-3.5f; s.MaxAnisotropy=i+1;
        s.ComparisonFunc=D3D12_COMPARISON_FUNC(i+1);
        for (UINT c=0;c<4;++c) s.BorderColor[c]=(i*4+c)/32.0f;
        s.MinLOD=float(i); s.MaxLOD=float(i)+2.75f;
    }
    Receipts cpu;
    cpu.Record(0x8000,1,original);
    UINT bank=99;
    if (!Receipts::Bank(0x1000,0x1100,32,bank) || bank!=1) return 1;
    Receipts::Receipt copy{};
    if (!cpu.Take(0x8000,bank,copy) || !copy.serial || copy.owner!=0x8000) return 2;
    if (std::memcmp(copy.descriptors.data(),original.data(),sizeof(original))!=0) return 3;
    if (cpu.Take(0x8000,bank,copy)) return 4;
    Receipts other; other.Record(0x8000,1,original);
    if (other.Take(0x9000,1,copy)) return 5;
    ConvertedSamplerBanks banks;
    banks.Protect(1);
    if (banks.ForRead(1)!=1) return 6;
    // Four and eight filtering changes before the draw has retired. Both cover each logical bank.
    for (UINT step=0;step<8;++step) {
        const UINT logical=(step+2)%4;
        const UINT physical=banks.ForWrite(logical);
        if (physical!=(logical==1 ? 5u : logical) || banks.ForRead(logical)!=physical || physical==4) return 7;
    }
    banks.Retire();
    if (banks.ForRead(1)!=5 || banks.ForWrite(2)!=2 || banks.ForRead(1)!=5) return 8;
    if (banks.ForWrite(1)!=1 || banks.ForRead(1)!=1) return 9;
    ConvertedOutput t;
    t.state=ConvertedOutput::State::pending; t.serial=14;
    if (!t.Claim() || t.Claim() || t.state!=ConvertedOutput::State::empty) return 10;
    t.state=ConvertedOutput::State::bound; t.fence=4;
    t.Cancel();
    if (t.state!=ConvertedOutput::State::bound || t.Retired(UINT64_MAX) || t.Retired(3) || !t.Retired(4)) return 11;
    t.failed=true;
    if (t.Retired(5)) return 12;
    if (!cpu.StillCurrent(0x8000,bank,copy.serial) || cpu.StillCurrent(0x9000,bank,copy.serial) ||
        cpu.StillCurrent(0x8000,2,copy.serial)) return 13;
    cpu.Record(0x8000,bank,original); // Identical descriptor bytes, new writer generation.
    if (cpu.StillCurrent(0x8000,bank,copy.serial) || cpu.Take(0x8000,bank,copy)) return 14;
    RecurringBorrowedRtv repeat;
    ConvertedOutput otherSlot;
    repeat.slot=&t; repeat.frame=7; repeat.epoch=80; repeat.serial=92; repeat.retired=1;
    if (repeat.CanStage(&t,8,81,93)) return 15;
    repeat.ready=true;
    if (!repeat.CanStage(&t,8,81,93) || repeat.CanStage(&otherSlot,8,81,93) ||
        repeat.CanStage(&t,7,81,93) || repeat.CanStage(&t,8,80,93) || repeat.CanStage(&t,8,81,92)) return 16;
    repeat.ReleaseCompleted(t,5); // failed owner cannot make its descriptors reusable
    if (repeat.ready) return 17;
    repeat.ready=true; repeat.retired=8;
    if (repeat.CanStage(&t,8,81,93) || !repeat.DetailNext()) return 18; // Stopped is terminal.
    RecurringBorrowedRtv persistent;
    persistent.slot=&t; persistent.ready=true; persistent.frame=7; persistent.epoch=80; persistent.serial=92;
    const UINT64 counts[]={8,9,1000000,UINT64_MAX};
    for (UINT64 count : counts) {
        persistent.retired=count;
        if (!persistent.CanStage(&t,8,81,93) || persistent.CanStage(&otherSlot,8,81,93) ||
            persistent.CanStage(&t,8,81,92) || persistent.DetailNext()!=(count<9)) return 19;
    }
    return 0;
}
