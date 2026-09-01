#pragma once

#include <stdint.h>
#include "gpu/gpu_memory.h"

/*
 * Generic DMA command ring.  GPU families supply their packet encoding above
 * this layer; the ring only owns aligned memory, bounds checks writes, and
 * makes a completed batch visible to a future device backend.
 */
typedef struct gpu_command_ring
{
    gpu_buffer buffer;
    uint32_t capacity_dwords;
    uint32_t write_dwords;
    uint32_t sealed;
} gpu_command_ring;

int gpu_ring_init(gpu_command_ring *ring, uint32_t bytes);
void gpu_ring_release(gpu_command_ring *ring);
int gpu_ring_emit(gpu_command_ring *ring, uint32_t dword);
int gpu_ring_emit_many(gpu_command_ring *ring, const uint32_t *dwords,
                       uint32_t count);
int gpu_ring_seal(gpu_command_ring *ring);

/* Reopens a ring only after its prior sealed batch has completed and the
 * caller has stopped the device from fetching it.  This does not free or
 * remap the backing allocation, so a pre-mapped kernel submission ring can
 * be safely reused between GX power leases. */
int gpu_ring_reset(gpu_command_ring *ring);
