#pragma once

#include <stdint.h>
#include "bootinfo.h"
#include "gpu/gpu_mmio.h"

/*
 * Read-only view of Qualcomm's firmware-owned RPMh command database.  It is
 * deliberately independent of both the GPU driver and the RSC transport:
 * consumers ask for a named resource and later hand the resulting address to
 * the platform-power service.
 */
typedef struct rpmh_cmd_db
{
    const uint8_t *bytes;
    uint32_t size_bytes;
    uint32_t data_offset;
} rpmh_cmd_db;

typedef struct rpmh_cmd_db_resource
{
    uint32_t address;
    uint16_t slave_id;
    const uint8_t *aux_data;
    uint16_t aux_size;
} rpmh_cmd_db_resource;

typedef struct rpmh_cmd_db_mapping
{
    rpmh_cmd_db db;
    gpu_mmio_window window;
    uint64_t physical_base;
    uint32_t size_bytes;
} rpmh_cmd_db_mapping;

/* Validates a firmware-provided Command DB buffer without issuing MMIO. */
int rpmh_cmd_db_open(rpmh_cmd_db *db, const void *bytes, uint32_t size_bytes);

/* Resource IDs are ASCII and at most eight bytes, for example "gfx.lvl". */
int rpmh_cmd_db_find(const rpmh_cmd_db *db, const char *resource_id,
                     rpmh_cmd_db_resource *out);

/* Maps and validates one firmware-reserved Command DB candidate read-only
 * from the driver's perspective.  A candidate is rejected unless the UEFI
 * memory map marks the complete range non-reclaimable, which prevents this
 * discovery path from treating ordinary RAM as firmware data. */
int rpmh_cmd_db_probe_firmware(const boot_info *boot, uint64_t physical_base,
                               uint32_t size_bytes,
                               rpmh_cmd_db_mapping *out);

void rpmh_cmd_db_mapping_release(rpmh_cmd_db_mapping *mapping);
