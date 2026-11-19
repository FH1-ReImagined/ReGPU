#include "../src/gpu/native_scene_blit.h"
#include <cassert>
using namespace regpu::native_scene;
int main() {
 ColorBlit b; BlitRequest r{};
 std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> retained;
 Microsoft::WRL::ComPtr<ID3D12Resource> output;
 assert(!b.CreateAndRecord(nullptr,nullptr,nullptr,0,nullptr,0,r,retained,output));
 assert(!output && retained.empty());
}
