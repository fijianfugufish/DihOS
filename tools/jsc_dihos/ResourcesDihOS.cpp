#include "config.h"
#include <wtf/CPUTime.h>
#include <wtf/MemoryFootprint.h>
#include "sacx_user_api.h"
#include "sacx_user_resources.h"

namespace WTF {
static bool resources(sacx_user_resources& value)
{
    return !sacx_user_resources_get(&value)
        && value.version == SACX_USER_RESOURCES_VERSION && value.bytes == sizeof(value)
        && value.flags == (SACX_USER_RESOURCES_CPU | SACX_USER_RESOURCES_MEMORY);
}
std::optional<CPUTime> CPUTime::get()
{
    sacx_user_resources value;
    if (!resources(value))
        return std::nullopt;
    return CPUTime { MonotonicTime::now(), Seconds(value.process_user_ns / 1e9),
        Seconds(value.process_kernel_ns / 1e9) };
}
Seconds CPUTime::forCurrentThread()
{
    sacx_user_resources value;
    if (!resources(value))
        sacx_user_exit(195, "WTF: native CPU accounting unavailable");
    return Seconds(value.thread_user_ns / 1e9 + value.thread_kernel_ns / 1e9);
}
size_t memoryFootprint()
{
    sacx_user_resources value;
    if (!resources(value))
        sacx_user_exit(195, "WTF: native memory accounting unavailable");
    return value.private_bytes;
}
}
