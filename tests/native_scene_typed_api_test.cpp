#include "regpu/native_scene.h"
#include "regpu/regpu.h"
#include <cassert>
namespace regpu { Config& GetConfig(){static Config c;return c;} }
int main() {using namespace regpu;
 assert(GetNativeSceneSize().width==0 && GetNativeSceneSize().height==0);
 assert(SetNativeSceneSize(1920,0)==NativeSceneResult::InvalidPair);
 assert(SetNativeSceneSize(16385,720)==NativeSceneResult::InvalidPair);
 assert(SetNativeSceneSize(1280,5761)==NativeSceneResult::InvalidPair);
 GetConfig().upscaler=Config::Upscaler::Fsr1;
 assert(SetNativeSceneSize(1920,1080)==NativeSceneResult::IncompatibleUpscaler);
 assert(GetNativeSceneSize().width==0);
 GetConfig().upscaler=Config::Upscaler::Off;
 assert(SetNativeSceneSize(2501,751)==NativeSceneResult::Accepted);
 assert(GetNativeSceneSize().width==2501 && GetNativeSceneSize().height==751);
 assert(SetNativeSceneSize(0,1)==NativeSceneResult::InvalidPair);
 assert(GetNativeSceneSize().width==2501 && GetNativeSceneSize().height==751);
 assert(SetNativeSceneSize(0,0)==NativeSceneResult::Accepted);
 assert(GetNativeSceneSize().width==0 && GetNativeSceneSize().height==0);
 detail::LockNativeSceneSize();
 assert(SetNativeSceneSize(1280,720)==NativeSceneResult::AlreadyStarted);
 assert(GetNativeSceneSize().width==0);
}
