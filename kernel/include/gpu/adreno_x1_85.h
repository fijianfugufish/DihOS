#pragma once

#include <stdint.h>
#include "gpu/gpu_firmware.h"
#include "gpu/gpu_bringup.h"
#include "gpu/gpu_cp.h"
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

/* Gen7 CP offsets are recorded in Mesa/Freedreno as dword register indices;
 * DihOS MMIO accesses use bytes, hence the explicit x4 conversion here. */
#define ADRENO_X1_85_CP_RB_RPTR           (0x0806u * 4u)
#define ADRENO_X1_85_CP_RB_WPTR           (0x0807u * 4u)
#define ADRENO_X1_85_CP_SQE_CNTL          (0x0808u * 4u)
#define ADRENO_X1_85_CP_HW_FAULT          (0x0821u * 4u)
#define ADRENO_X1_85_CP_PROTECT_STATUS    (0x0824u * 4u)
#define ADRENO_X1_85_CP_PROTECT_CNTL      (0x084fu * 4u)
#define ADRENO_X1_85_CP_PROTECT_BASE      (0x0850u * 4u)
#define ADRENO_X1_85_CP_RB_BASE            (0x0800u * 4u)
#define ADRENO_X1_85_CP_RB_CNTL            (0x0802u * 4u)
#define ADRENO_X1_85_CP_RB_RPTR_ADDR       (0x0804u * 4u)
#define ADRENO_X1_85_CP_ADDR_MODE_CNTL     (0x0842u * 4u)
#define ADRENO_X1_85_CP_APRIV_CNTL         (0x0844u * 4u)
#define ADRENO_X1_85_CP_SQE_INSTR_BASE     (0x0830u * 4u)
#define ADRENO_X1_85_CP_BV_RB_RPTR_ADDR    (0x0A98u * 4u)
#define ADRENO_X1_85_CP_BV_APRIV_CNTL      (0x0AD0u * 4u)
#define ADRENO_X1_85_CP_LPAC_APRIV_CNTL    (0x0B31u * 4u)
#define ADRENO_X1_85_GMU_AHB_FENCE_STATUS  (0x9313u * 4u)

/* 32 KiB ring and 32-byte blocks.  Gen7.2 has hardware RPTR shadow support;
 * the boot path provides both BR and BV shadow locations before enabling CP. */
#define ADRENO_X1_85_CP_RB_CNTL_BOOT       0x0000020Cu
#define ADRENO_X1_85_CP_ADDR_MODE_64BIT     0x00000001u
#define ADRENO_X1_85_CP_BR_APRIV_MASK       0x0000003Fu
#define ADRENO_X1_85_CP_AUX_APRIV_MASK      0x0000000Fu

/* Gen7.2 host-side state that must be established after GX is live and
 * before CP is allowed to fetch either SQE firmware or ring commands.  The
 * upstream register database specifies these in dword indices; retain the
 * byte conversion here so the MMIO layer cannot be called with units mixed. */
#define ADRENO_X1_85_GBIF_HALT               (0x3c45u * 4u)
#define ADRENO_X1_85_GBIF_HALT_ACK           (0x3c46u * 4u)
#define ADRENO_X1_85_GBIF_QSB_SIDE0           (0x3c03u * 4u)
#define ADRENO_X1_85_GBIF_QSB_SIDE1           (0x3c04u * 4u)
#define ADRENO_X1_85_GBIF_QSB_SIDE2           (0x3c05u * 4u)
#define ADRENO_X1_85_GBIF_QSB_SIDE3           (0x3c06u * 4u)
#define ADRENO_X1_85_RBBM_GBIF_HALT          (0x0016u * 4u)
#define ADRENO_X1_85_RBBM_SECVID_TSB_BASE    (0xf800u * 4u)
#define ADRENO_X1_85_RBBM_SECVID_TSB_SIZE    (0xf802u * 4u)
#define ADRENO_X1_85_RBBM_SECVID_TSB_CNTL    (0xf803u * 4u)
#define ADRENO_X1_85_RBBM_GBIF_QOS           (0x0011u * 4u)
#define ADRENO_X1_85_RBBM_INT_CLEAR          (0x0037u * 4u)
#define ADRENO_X1_85_RBBM_INT_MASK           (0x0038u * 4u)
#define ADRENO_X1_85_RBBM_INT_STATUS         (0x0036u * 4u)
#define ADRENO_X1_85_RBBM_BUSY_MASK           (0x050bu * 4u)
#define ADRENO_X1_85_RBBM_PERFCTR_CNTL        (0x0500u * 4u)
#define ADRENO_X1_85_RBBM_INTERFACE_HANG_CNTL (0x001fu * 4u)
#define ADRENO_X1_85_UCHE_CACHE_WAYS         (0x0e17u * 4u)
#define ADRENO_X1_85_UCHE_CLIENT_PF           (0x0e19u * 4u)
#define ADRENO_X1_85_TPL1_NC_MODE_CNTL         (0xb604u * 4u)
#define ADRENO_X1_85_SP_NC_MODE_CNTL           (0xae02u * 4u)
#define ADRENO_X1_85_CP_DBG_ECO_CNTL           (0x0843u * 4u)
#define ADRENO_X1_85_UCHE_WRITE_THRU_BASE    (0x0e07u * 4u)
#define ADRENO_X1_85_UCHE_TRAP_BASE          (0x0e09u * 4u)
#define ADRENO_X1_85_UCHE_GMEM_RANGE_MIN     (0x0e0bu * 4u)
#define ADRENO_X1_85_UCHE_GMEM_RANGE_MAX     (0x0e0du * 4u)
#define ADRENO_X1_85_UCHE_GBIF_GX_CONFIG     (0x0e3au * 4u)
#define ADRENO_X1_85_UCHE_CMDQ_CONFIG        (0x0e3cu * 4u)
#define ADRENO_X1_85_CP_AHB_CNTL             (0x098du * 4u)
#define ADRENO_X1_85_CP_INTERRUPT_STATUS     (0x0822u * 4u)
#define ADRENO_X1_85_CP_CP2GMU_STATUS        (0x0812u * 4u)
#define ADRENO_X1_85_CP_ROQ_RB_STATUS        (0x0939u * 4u)
#define ADRENO_X1_85_RB_CMP_DBG_ECO_CNTL     (0x8e28u * 4u)
#define ADRENO_X1_85_TPL1_BICUBIC_BASE        (0xb608u * 4u)

/* Keep the fault sources visible while the first CP submission is being
 * diagnosed.  This is the A7xx RBBM interrupt mask from the upstream DRM
 * driver, expressed without kernel-only bit helpers. */
#define ADRENO_X1_85_RBBM_INT_MASK_BOOT      0x33d283c2u

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
const gpu_cp_register_layout *adreno_x1_85_cp_layout(void);

/* Programs the non-firmware-owned Gen7.2 state required for CP memory
 * access.  This must be called only with a live GX lease, and remains
 * separate from gpu_cp_bind so a future GPU profile can supply its own
 * hardware-init sequence. */
int adreno_x1_85_prepare_cp_host(const gpu_mmio_window *gfx);

/* Builds the CPU/GPU spinlock record consumed by A7xx CP_ME_INIT.  The
 * record is populated only from CP registers that this profile owns and
 * has already programmed; it is then cache-cleaned before submission. */
int adreno_x1_85_build_cp_pwrup_record(const gpu_mmio_window *gfx,
                                       gpu_buffer *record);

/* Emits the first CP-owned work for Gen7.2.  `pwrup_record_iova` identifies
 * the mapped spinlock/register-init record prepared for this live GX lease. */
int adreno_x1_85_emit_minimal_cp_init(gpu_command_ring *ring,
                                      uint64_t pwrup_record_iova);

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
