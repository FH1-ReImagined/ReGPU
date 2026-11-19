#include "../src/gpu/native_scene_private_pair_readback.h"
#include "../src/gpu/native_scene_private_samples.h"
#include <array>
#include <cassert>

int main() {
    auto classification=[](bool samplesReadable, UINT64 passed, bool planesReadable, bool changed,
                           const char* expected) {
        assert(std::strcmp(PrivatePassedSamples::Classify(samplesReadable,passed,planesReadable,changed),
                           expected)==0);
    };
    classification(false,0,true,false,"unavailable");
    classification(true,1,false,false,"unavailable");
    classification(true,0,true,false,"no_passing_samples");
    classification(true,0,true,true,"zero_samples_changed");
    classification(true,13,true,false,"passed_unchanged");
    classification(true,17,true,true,"passed_changed");
    const D3D12_RECT box{2, 1, 5, 3};
    std::array<unsigned char, 64> before{};
    std::array<unsigned char, 256> after{};
    // Nonzero origin; offsets 0..23 in each compact row correspond to x=2..4.
    before[24] = 1;                 // compact row padding
    after[0] = after[64 + 40] = 2;  // outside ROI
    auto compare = [&] { return PrivateDrawColorBefore::CompareRows(before.data(), 32,
                                                after.data(), 64, box); };
    auto result = compare();
    assert(result.readable && result.pixels == 6 && result.changed == 0);
    after[64 + 16] = after[64 + 17] = 1; // two bytes of the same first pixel
    after[128 + 39] = 1;                 // last byte of the last pixel
    result = compare();
    assert(result.readable && result.pixels == 6 && result.changed == 2);
    assert(!PrivateDrawColorBefore::CompareRows(nullptr, 32, after.data(), 64, box).readable);
    assert(!PrivateDrawColorBefore::CompareRows(before.data(), 16, after.data(), 64, box).readable);
    assert(!PrivateDrawColorBefore::CompareRows(before.data(), 32, after.data(), 32, box).readable);

    // A packed 8-byte stencil footprint stores only its fifth byte as stencil;
    // depth occupies the first four. Ignore padding, neighboring pixels, and
    // changes to the other plane when counting a nonzero-origin rectangle.
    PrivatePairReadback rb;
    rb.bytes=512;
    auto setup=[&](unsigned image, unsigned plane, unsigned offset, unsigned stride) {
        auto& i=rb.image[image];
        i.planes=(image&1) ? 2 : 1;
        i.fp[plane].Offset=offset;
        i.fp[plane].Footprint.Width=4;
        i.fp[plane].Footprint.RowPitch=48;
        i.rowBytes[plane]=4*stride;
        i.rows[plane]=2;
    };
    setup(0,0,0,8); setup(1,0,96,8); setup(1,1,192,8);
    setup(2,0,288,8); setup(3,0,384,4); setup(3,1,480,1);
    rb.image[3].rows[1]=1;
    rb.image[3].fp[1].Footprint.RowPitch=32;
    std::array<unsigned char,512> a{}, b{};
    const D3D12_RECT crop{1,0,3,1};
    b[8]=b[9]=1; b[31]=1; b[39]=1;
    b[96+8+4]=1; b[96+16+3]=1;
    b[192+8+4]=1; b[192+16]=1;
    b[288+16]=1; b[384+8]=1; b[480+1]=1;
    UINT64 count=0;
    for (unsigned image=0;image<4;++image)
        for (unsigned plane=0;plane<rb.image[image].planes;++plane) {
            assert(PrivatePairedDelta::Count(rb,a.data(),rb,b.data(),image,plane,crop,count));
            assert(count==1);
        }
    rb.bytes=511;
    assert(!PrivatePairedDelta::Count(rb,a.data(),rb,b.data(),3,1,crop,count));
    rb.bytes=512;
    rb.image[1].rowBytes[1]=12;
    assert(!PrivatePairedDelta::Count(rb,a.data(),rb,b.data(),1,1,crop,count));
}
