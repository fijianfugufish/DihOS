#include "config.h"
#include <wtf/OSAllocator.h>
#include "sacx_user_api.h"
extern "C" int dihos_user_main(const sacx_api*){
    // Aligned but unowned: the real sparse-VM facade must reject it safely.
    WTF::OSAllocator::decommit(reinterpret_cast<void*>(0x1000),4096);
    return 179;
}
