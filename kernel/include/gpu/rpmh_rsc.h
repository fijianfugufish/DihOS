#pragma once

#include <stdint.h>
#include "gpu/gpu_mmio.h"

/*
 * Read-only RPMh RSC identity probe.  The caller maps the single controller
 * page, flushes its breadcrumb, then this function reads only offset zero.
 * It is intentionally not a power-vote or command-submission interface.
 */
typedef struct rpmh_rsc_identity
{
    uint32_t raw_id;
    uint8_t major_version;
    uint8_t minor_version;
} rpmh_rsc_identity;

int rpmh_rsc_probe_identity(const gpu_mmio_window *window,
                            rpmh_rsc_identity *out);
