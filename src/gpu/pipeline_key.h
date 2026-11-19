#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace regpu::pipeline {

inline constexpr uint32_t kNoDepthClip = 0x004;
inline constexpr uint32_t kMotion = 0x400;
inline constexpr uint32_t kChangedFlags = kNoDepthClip | kMotion;

struct Key {
    uint64_t vs, ps;
    uint32_t blend, depth, stencilMaskMode, flags;
    uint32_t biasScale = 0, biasOffset = 0;
    bool operator==(const Key&) const = default;
};

inline const char* ManifestPrefix(uint32_t flags) {
    return flags & kChangedFlags ? "K2 " : "P ";
}

inline const wchar_t* LibraryPrefix(uint32_t flags) {
    return flags & kChangedFlags ? L"K2_" : L"";
}

// Local warmup and shipped seeds replay base shaders. Motion variants need the
// real motion shaders from GetPipeline. An old row with bit 2 is ambiguous.
inline bool ParseBasePipeline(const char* line, Key& out) {
    const bool v2 = std::strncmp(line, "K2 ", 3) == 0;
    if (!v2 && line[0] != 'P') return false;

    Key key{};
    unsigned long long vs, ps;
    const int count = std::sscanf(line + (v2 ? 3 : 1), "%llx %llx %x %x %x %x %x %x",
        &vs, &ps, &key.blend, &key.depth, &key.stencilMaskMode, &key.flags, &key.biasScale, &key.biasOffset);
    if (count < 6 || (key.flags & kMotion) || (!v2 && (key.flags & kNoDepthClip))) return false;
    key.vs = vs;
    key.ps = ps;
    out = key;
    return true;
}

} // namespace regpu::pipeline
