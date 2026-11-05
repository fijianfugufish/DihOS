#pragma once
#include <wtf/RefPtr.h>
namespace WebCore {
// Typed ownership contract only. DihOSImage's pixel storage/rendering backend
// must be defined before NativeImage can be implemented or instantiated.
class DihOSImage;
using PlatformImagePtr = WTF::RefPtr<DihOSImage>;
}
