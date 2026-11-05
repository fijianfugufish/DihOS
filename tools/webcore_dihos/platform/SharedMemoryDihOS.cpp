#include "config.h"
#include "SharedMemory.h"
#include <sacx_user_vm.h>
#include <wtf/Assertions.h>
#include <wtf/Ref.h>
#include <wtf/RefPtr.h>

namespace WebCore {
// Only process-owned storage is available. There is no DihOS IPC/memory-handle
// service yet, so exporting and mapping transferable handles must fail.
RefPtr<SharedMemory> SharedMemory::allocate(size_t size)
{
    if (!size || size > SIZE_MAX - 4095)
        return nullptr;
    size_t bytes = (size + 4095) & ~static_cast<size_t>(4095);
    void* memory = dihos_user_vm_reserve(bytes, 4096, 0);
    if (!memory)
        return nullptr;
    if (dihos_user_vm_commit(memory, bytes, true)) {
        RELEASE_ASSERT(!dihos_user_vm_release(memory, bytes));
        return nullptr;
    }
    auto result = adoptRef(*new SharedMemory);
    result->m_data = memory;
    result->m_size = size;
    return result;
}

SharedMemory::~SharedMemory()
{
    if (m_data) {
        size_t bytes = (m_size + 4095) & ~static_cast<size_t>(4095);
        RELEASE_ASSERT(!dihos_user_vm_release(m_data, bytes));
    }
}

std::optional<SharedMemory::Handle> SharedMemory::createHandle(Protection)
{
    return std::nullopt;
}

RefPtr<SharedMemory> SharedMemory::map(Handle&&, Protection, CopyOnWrite)
{
    return nullptr;
}
}
