#include "gpu/pipeline_key.h"
#include <cstring>
#include <cwchar>
#include <initializer_list>

using namespace regpu::pipeline;

int main() {
    static_assert(!(kMotion & (kNoDepthClip | 0x303u)));
    static_assert(!(kNoDepthClip & 0x303u));
    for (unsigned mode=0; mode<4; ++mode) {
        const bool disableClip=mode&1, motion=mode&2;
        const auto flags=0x303u | (disableClip ? kNoDepthClip : 0u) | (motion ? kMotion : 0u);
        if (!!(flags & kNoDepthClip)!=disableClip || !!(flags & kMotion)!=motion) return 1;
        if ((std::wcscmp(LibraryPrefix(flags),L"K2_")==0)!=(disableClip || motion)) return 2;
        char line[128];
        std::snprintf(line,sizeof(line),"%s11 22 3 4 5 %x a b",ManifestPrefix(flags),flags);
        Key key{};
        if (ParseBasePipeline(line,key)==motion) return 3;
        if (!motion && (key.vs!=0x11 || key.ps!=0x22 || key.flags!=flags || key.biasScale!=10 || key.biasOffset!=11)) return 4;
        // An older reader cannot accidentally accept a new K2 row as an old P.
        unsigned long long legacyVs;
        if ((std::sscanf(line,"P %llx",&legacyVs)==1)!=(mode==0)) return 5;
    }
    Key key{};
    if (!ParseBasePipeline("P 11 22 3 4 5 102",key) || key.biasScale || key.biasOffset) return 6;
    if (!ParseBasePipeline("P 11 22 3 4 5 202 a",key) || key.biasScale!=10 || key.biasOffset) return 7;
    Key before=key;
    for (const char* bad: {"P 11 22 3 4 5 4","P 11 22 3 4 5 104","P 11 22 3 4 5 400",
                            "K2 11 22 3 4 5 404","K2 11","K3 11 22 3 4 5 0","V 11 22"})
        if (ParseBasePipeline(bad,key) || !(key==before)) return 8;
    return 0;
}
