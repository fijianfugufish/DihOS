#include "gpu/gpu_memory.h"
#include "asm/asm.h"
#include "memory/pmem.h"

#define GPU_PAGE_SIZE 4096ull

static void zero_bytes(void *ptr, uint64_t bytes)
{
    uint8_t *p = (uint8_t *)ptr;
    while (bytes--)
        *p++ = 0u;
}

int gpu_buffer_alloc(gpu_buffer *out, uint64_t size_bytes, uint32_t flags)
{
    gpu_buffer buffer = {0};

    if (!out || !size_bytes)
        return -1;

    buffer.pages = (size_bytes + GPU_PAGE_SIZE - 1u) / GPU_PAGE_SIZE;
    buffer.cpu = pmem_alloc_pages(buffer.pages);
    if (!buffer.cpu)
        return -2;

    buffer.phys = pmem_virt_to_phys(buffer.cpu);
    buffer.size_bytes = size_bytes;
    buffer.flags = flags;
    /* iova intentionally remains zero until the SMMU domain mapper owns it. */
    if (flags & GPU_BUFFER_ZEROED)
        zero_bytes(buffer.cpu, buffer.pages * GPU_PAGE_SIZE);
    *out = buffer;
    return 0;
}

int gpu_buffer_import(gpu_buffer *out, void *cpu, uint64_t phys,
                      uint64_t size_bytes, uint32_t flags)
{
    if (!out || !cpu || !phys || !size_bytes ||
        (phys & (GPU_PAGE_SIZE - 1u)))
        return -1;
    *out = (gpu_buffer){
        cpu, phys, 0u, size_bytes,
        (size_bytes + GPU_PAGE_SIZE - 1u) / GPU_PAGE_SIZE,
        flags | GPU_BUFFER_EXTERNAL};
    return 0;
}

void gpu_buffer_release(gpu_buffer *buffer)
{
    if (!buffer || !buffer->cpu)
        return;
    if (!(buffer->flags & GPU_BUFFER_EXTERNAL))
        pmem_free_pages(buffer->cpu, buffer->pages);
    *buffer = (gpu_buffer){0};
}

void gpu_buffer_prepare_for_device(const gpu_buffer *buffer)
{
    if (!buffer || !buffer->cpu || !buffer->size_bytes)
        return;
    asm_dma_clean_range(buffer->cpu, buffer->size_bytes);
}
