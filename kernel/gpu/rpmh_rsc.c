#include "gpu/rpmh_rsc.h"

int rpmh_rsc_probe_identity(const gpu_mmio_window *window,
                            rpmh_rsc_identity *out)
{
    uint32_t raw_id;

    if (!window || !out || !window->cpu_mapped || !window->cpu_base ||
        window->size_bytes < 4u)
        return -1;
    raw_id = *(volatile uint32_t *)(void *)window->cpu_base;
    *out = (rpmh_rsc_identity){
        raw_id, (uint8_t)(raw_id >> 16), (uint8_t)(raw_id >> 8)};
    return 0;
}
