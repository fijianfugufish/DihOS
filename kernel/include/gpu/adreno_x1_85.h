#pragma once

#include <stdint.h>
#include "gpu/gpu_firmware.h"
#include "gpu/gpu_bringup.h"
#include "gpu/gpu_gmu_hfi.h"
#include "gpu/gpu_iommu.h"
#include "gpu/gpu_mmio.h"
#include "gpu/rpmh_cmd_db.h"

/*
 * Lenovo CRD08380 / Snapdragon 8380 profile, recorded from its ACPI GPU0
 * _CRS and IORT tables.  This header contains discovery facts only: it must
 * not be used to perform a GPU register access until the power, firmware and
 * SMMU layers have been brought up.
 */
#define ADRENO_X1_85_GFX_REGS_BASE       0x0000000003D00000ull
#define ADRENO_X1_85_GFX_REGS_SIZE       0x000000000009F000ull
#define ADRENO_X1_85_PDC_REGS_BASE       0x000000000B280000ull
#define ADRENO_X1_85_PDC_REGS_SIZE       0x0000000000000100ull
/* Windows exposes only the PDC's first ACPI register range.  The X1E
 * hardware description used by the upstream driver declares the complete
 * 64 KiB PDC block; its enable and sequencer controls live at +0x4500. */
#define ADRENO_X1_85_PDC_MMIO_SIZE       0x0000000000010000ull
#define ADRENO_X1_85_GPU_SMMU_BASE       0x0000000003DA0000ull
#define ADRENO_X1_85_GPU_SMMU_SIZE       0x0000000000040000ull
#define ADRENO_X1_85_SYSTEM_SMMU_BASE    0x0000000015000000ull
#define ADRENO_X1_85_GFX_SPI             0x14Cu
#define ADRENO_X1_85_GMU_HOST_SPI        0x150u
#define ADRENO_X1_85_LPAC_SPI            0x14Du
/* The Gen7 GMU consumes this firmware-facing identifier during cold boot.
 * It is distinct from the RBBM hardware-version register. */
#define ADRENO_X1_85_GMU_CHIP_ID         0x07050001u
/* The firmware-owned AOP command database is a reserved 128 KiB region on
 * X1E.  DihOS verifies the UEFI descriptor and command-DB magic before it
 * maps or consumes it; this is a probe candidate, never a blind RAM read. */
#define ADRENO_X1_85_CMD_DB_BASE          0x0000000081C60000ull
#define ADRENO_X1_85_CMD_DB_SIZE          0x00020000u

/* RBBM is the always-present root block in the GPU aperture.  These reads
 * are the first preflight only; neither one changes GPU state. */
#define ADRENO_X1_85_RBBM_HW_VERSION      0x0000u
#define ADRENO_X1_85_RBBM_STATUS          0x0010u

/* X1E GPU sub-blocks. Their ranges are constrained against GPU0's ACPI
 * aperture before a driver is allowed to use them. */
#define ADRENO_X1_85_RSCC_BASE            0x0000000003D50000ull
#define ADRENO_X1_85_RSCC_SIZE            0x0000000000010000ull
#define ADRENO_X1_85_GMU_BASE             0x0000000003D6A000ull
#define ADRENO_X1_85_GMU_SIZE             0x0000000000035000ull
#define ADRENO_X1_85_GPUCC_BASE           0x0000000003D90000ull
#define ADRENO_X1_85_GPUCC_SIZE           0x000000000000A000ull

typedef struct adreno_x1_85_profile
{
    uint64_t gfx_regs_base;
    uint64_t gfx_regs_size;
    uint64_t pdc_regs_base;
    uint64_t pdc_regs_size;
    uint64_t gpu_smmu_base;
    uint64_t system_smmu_base;
    uint32_t gfx_spi;
    uint32_t gmu_host_spi;
    uint32_t lpac_spi;
    uint8_t acpi_profile_present;
} adreno_x1_85_profile;

typedef struct adreno_x1_85_block_map
{
    uint64_t rscc_base;
    uint64_t rscc_size;
    uint64_t gmu_base;
    uint64_t gmu_size;
    uint64_t gpucc_base;
    uint64_t gpucc_size;
} adreno_x1_85_block_map;

/* Read-only, no-MMIO discovery. Returns 0 only when an ACPI RSDP is present. */
int adreno_x1_85_probe_profile(uint64_t rsdp_phys, adreno_x1_85_profile *out);
int adreno_x1_85_resolve_iommu(uint64_t rsdp_phys,
                                gpu_iommu_topology *out);
int adreno_x1_85_resolve_blocks(const adreno_x1_85_profile *profile,
                                adreno_x1_85_block_map *out);
const gpu_bringup_plan *adreno_x1_85_bringup_plan(void);

/* Programs the documented X1E CX-side, RSCC and PDC prerequisites for a Gen7
 * GMU cold boot.  It does not release reset, enable GX, or submit GPU work. */
int adreno_x1_85_prepare_gmu_cold_boot(const gpu_mmio_window *gfx,
                                       const gpu_mmio_window *gmu,
                                       const gpu_mmio_window *rscc,
                                       const gpu_mmio_window *pdc);

/* The immutable OPP half of X1E's Gen7 HFI setup.  It contains only the
 * frequencies and RPMh regulator levels documented for this SoC.  The
 * platform must still provide the firmware-owned command-DB addresses and
 * BCM payloads needed for the separate bandwidth table. */
const gpu_gmu_hfi_gen7_perf_table *adreno_x1_85_hfi_perf_table(void);

/* Converts the profile's RPMh OPP levels into the packed ARC votes required
 * by this machine's GMU firmware.  The level-to-index mappings belong to the
 * firmware command database and are intentionally not hard-coded. */
int adreno_x1_85_build_hfi_perf_table(const rpmh_cmd_db *cmd_db,
                                      gpu_gmu_hfi_gen7_perf_table *out);

/* Builds the firmware-specific half of the Gen7 bandwidth table.  Resource
 * addresses, units, widths and virtual-clock groups are read from the live
 * RPMh command database; only the X1E OPP bandwidth policy lives here. */
int adreno_x1_85_build_hfi_bw_table(const rpmh_cmd_db *cmd_db,
                                    gpu_gmu_hfi_gen7_bw_table *out);

/* Manifest only: loading is deferred until the GMU transport is implemented. */
const gpu_firmware_manifest *adreno_x1_85_firmware_manifest(void);
