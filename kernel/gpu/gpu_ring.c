#include "gpu/gpu_ring.h"

int gpu_ring_init(gpu_command_ring *ring, uint32_t bytes)
{
    if (!ring || !bytes || (bytes & 3u))
        return -1;
    *ring = (gpu_command_ring){0};
    if (gpu_buffer_alloc(&ring->buffer, bytes,
                         GPU_BUFFER_COMMAND | GPU_BUFFER_ZEROED) != 0)
        return -2;
    ring->capacity_dwords = bytes / 4u;
    return 0;
}

void gpu_ring_release(gpu_command_ring *ring)
{
    if (!ring)
        return;
    gpu_buffer_release(&ring->buffer);
    *ring = (gpu_command_ring){0};
}

int gpu_ring_emit(gpu_command_ring *ring, uint32_t dword)
{
    uint32_t *words;

    if (!ring || !ring->buffer.cpu || ring->sealed ||
        ring->write_dwords >= ring->capacity_dwords)
        return -1;
    words = (uint32_t *)ring->buffer.cpu;
    words[ring->write_dwords++] = dword;
    return 0;
}

int gpu_ring_emit_many(gpu_command_ring *ring, const uint32_t *dwords,
                       uint32_t count)
{
    if (!ring || !dwords || ring->sealed ||
        count > ring->capacity_dwords - ring->write_dwords)
        return -1;
    for (uint32_t i = 0u; i < count; ++i)
        if (gpu_ring_emit(ring, dwords[i]) != 0)
            return -2;
    return 0;
}

int gpu_ring_seal(gpu_command_ring *ring)
{
    if (!ring || !ring->buffer.cpu || ring->sealed)
        return -1;
    gpu_buffer_prepare_for_device(&ring->buffer);
    ring->sealed = 1u;
    return 0;
}

int gpu_ring_reset(gpu_command_ring *ring)
{
    if (!ring || !ring->buffer.cpu || !ring->capacity_dwords)
        return -1;
    ring->write_dwords = 0u;
    ring->sealed = 0u;
    return 0;
}
