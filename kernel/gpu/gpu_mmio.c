#include "gpu/gpu_mmio.h"
#include "memory/mmio_map.h"
#include "asm/asm.h"

int gpu_mmio_map_window(gpu_mmio_window *window, uint64_t phys_base,
                        uint64_t size_bytes)
{
    if (!window || !phys_base || !size_bytes)
        return -1;
    *window = (gpu_mmio_window){0};
    if (mmio_map_device_identity(phys_base, size_bytes) != 0)
        return -2;
    window->phys_base = phys_base;
    window->size_bytes = size_bytes;
    window->cpu_base = (volatile uint8_t *)(uintptr_t)phys_base;
    window->cpu_mapped = 1u;
    return 0;
}

int gpu_mmio_try_read32(const gpu_mmio_window *window, uint32_t offset,
                        uint32_t *out_value)
{
    if (!window || !out_value || !window->cpu_mapped || !window->cpu_base ||
        offset > window->size_bytes || window->size_bytes - offset < 4u ||
        (offset & 3u))
        return -1;
    return asm_aa64_try_read32((uint64_t)(uintptr_t)window->cpu_base + offset,
                               out_value);
}

int gpu_mmio_try_write32(const gpu_mmio_window *window, uint32_t offset,
                         uint32_t value)
{
    if (!window || !window->cpu_mapped || !window->cpu_base ||
        offset > window->size_bytes || window->size_bytes - offset < 4u ||
        (offset & 3u))
        return -1;
    return asm_aa64_try_write32((uint64_t)(uintptr_t)window->cpu_base + offset,
                                value);
}

void gpu_mmio_unmap_window(gpu_mmio_window *window)
{
    /* The early DihOS mapper has no unmap primitive yet. Clear the driver's
     * capability so a released device cannot be accessed through this handle. */
    if (window)
        *window = (gpu_mmio_window){0};
}
