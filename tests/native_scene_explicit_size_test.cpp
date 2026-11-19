#include "../src/gpu/native_scene_size.h"
#include <cassert>
using namespace regpu::native_scene;
int main(){assert(TransferFor({1920,540},{1280,720})==Transfer::Resample);assert(TransferFor({1920,1080},{1280,720})==Transfer::Resample);assert(TransferFor({1280,720},{1280,720})==Transfer::Copy);assert(TransferFor({0,720},{1280,720})==Transfer::Invalid);}
