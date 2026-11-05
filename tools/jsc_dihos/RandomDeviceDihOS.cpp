#include "config.h"
#include <wtf/RandomDevice.h>
#include "sacx_user_random.h"
#include "sacx_user_api.h"

namespace WTF {
void RandomDevice::cryptographicallyRandomValues(std::span<uint8_t> buffer)
{
    if (sacx_user_random(buffer.data(), buffer.size()))
        sacx_user_exit(185, "WTF: trusted kernel entropy unavailable");
}
}
