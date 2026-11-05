// CMake probes and target-only metadata extractors use ordinary freestanding
// main symbols. None of these executables is run on the build host.
#include "sacx_user_api.h"
extern "C" int main(int,char**) __attribute__((weak));
extern "C" int cppMain2(int,char**) __asm__("_Z4mainiPPc") __attribute__((weak));
extern "C" int cppMain0() __asm__("_Z4mainv") __attribute__((weak));
extern "C" __attribute__((weak)) int dihos_user_main(const sacx_api*)
{
    char* arguments[] { nullptr };
    if(main)return main(0,arguments);
    if(cppMain2)return cppMain2(0,arguments);
    if(cppMain0)return cppMain0();
    sacx_user_exit(163,"Missing native feature-test main");
}
