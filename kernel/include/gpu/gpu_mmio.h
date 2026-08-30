#pragma once

#include <stdint.h>

/*
 * A CPU mapping is not permission to access a device.  Drivers receive a
 * window only after the architecture mapper has installed Device memory
 * attributes; the platform-power layer separately decides when reads and
 * writes are legal.
 */
typedef struct gpu_mmio_window
{
    uint64_t phys_base;
    uint64_t size_bytes;
    volatile uint8_t *cpu_base;
    uint32_t cpu_mapped;
} gpu_mmio_window;

/* Installs an identity CPU mapping with Device attributes. No peripheral
 * transaction is issued by this operation. */
int gpu_mmio_map_window(gpu_mmio_window *window, uint64_t phys_base,
                        uint64_t size_bytes);

/* Performs one 32-bit read through DihOS's temporary exception-vector guard.
 * A powered-off or inaccessible peripheral returns an error rather than
 * converting this first preflight into a fatal kernel data abort. */
int gpu_mmio_try_read32(const gpu_mmio_window *window, uint32_t offset,
                        uint32_t *out_value);

/* Guarded equivalent for a single 32-bit Device-memory write.  The caller
 * must still ensure that the target has been power/clock enabled and that
 * partial programming cannot expose an uninitialised device to DMA. */
int gpu_mmio_try_write32(const gpu_mmio_window *window, uint32_t offset,
                         uint32_t value);

void gpu_mmio_unmap_window(gpu_mmio_window *window);
