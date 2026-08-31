#include "gpu/gpu_core.h"
#include "gpu/adreno_x1_85.h"
#include "gpu/gpu_firmware.h"
#include "gpu/gpu_gmu_boot.h"
#include "gpu/gpu_gmu_image.h"
#include "gpu/gpu_gmu_memory.h"
#include "gpu/gpu_cp.h"
#include "gpu/gpu_iommu.h"
#include "gpu/gpu_mmio.h"
#include "gpu/gpu_qcom_scm.h"
#include "gpu/gpu_smmu_v2.h"
#include "gpu/gpu_ring.h"
#include "gpu/rpmh_cmd_db.h"
#include "hardware_probes/acpi_probe_net_candidates.h"
#include "terminal/terminal_api.h"

static gpu_device_info g_primary;
static gpu_firmware_set g_firmware;
static gpu_iommu_domain g_iommu_domain;
static gpu_iommu_topology g_iommu_topology;
static gpu_iommu_attach_plan g_iommu_attach_plan;
static gpu_command_ring g_submission_ring;
static gpu_buffer g_cp_shadow;
static gpu_buffer g_cp_pwrup;
static gpu_scanout_target g_scanout_target;
static gpu_mmio_window g_gpu_regs_window;
static gpu_mmio_window g_gpu_pdc_window;
static gpu_mmio_window g_gpu_smmu_window;
static gpu_mmio_window g_gpu_gmu_window;
static gpu_mmio_window g_gpu_rscc_window;
static gpu_smmuv2_caps g_gpu_smmu_caps;
static uint32_t g_gpu_smmu_context_bank;
static uint32_t g_gpu_smmu_bound_stream_count;
static uint32_t g_gmu_smmu_context_bank;
static uint32_t g_gmu_smmu_bound_stream_count;
static gpu_gmu_image g_gmu_image;
static gpu_gmu_memory g_gmu_memory;
static rpmh_cmd_db_mapping g_rpmh_cmd_db;
static gpu_gmu_hfi_gen7_perf_table g_hfi_perf_table;
static gpu_gmu_hfi_gen7_bw_table g_hfi_bw_table;
static uint32_t g_gmu_reset_signature;

#define GPU_SMMUV2_FSR_FAULT_MASK 0xC00001FEu

#define GPU_CORE_STAGING_IOVA_BASE 0x0000000010000000ull
#define GPU_CORE_SUBMISSION_IOVA   0x0000000020000000ull
#define GPU_CORE_SUBMISSION_BYTES  0x00008000u
#define GPU_CORE_CP_SHADOW_IOVA     0x0000000020010000ull
#define GPU_CORE_CP_PWRUP_IOVA      0x0000000020011000ull
#define GPU_CORE_SCANOUT_IOVA      0x0000000030000000ull

/* Gen7's hardware shadow layout is rptr, fence, then BV rptr.  The fence
 * slot is CPU bookkeeping, not a second hardware rptr destination. */
#define GPU_CORE_CP_BR_RPTR_OFFSET  0u
#define GPU_CORE_CP_BV_RPTR_OFFSET  8u

/* X1E exposes two EL1-programmable render stream-match slots.  Further
 * GPU-local IORT lanes are retained by the platform firmware; do not attempt
 * to claim them from DihOS because the SMMU correctly blocks that write. */
#define ADRENO_X1_85_RENDER_SID_COUNT 2u
#define ADRENO_X1_85_GMU_SID_COUNT    1u
#define ADRENO_X1_85_DRIVER_SID_COUNT \
    (ADRENO_X1_85_RENDER_SID_COUNT + ADRENO_X1_85_GMU_SID_COUNT)

static const uint32_t g_adreno_x1_85_render_sids[] = {0u, 1u};
static const uint32_t g_adreno_x1_85_gmu_sids[] = {5u};

/* ARM SMMUv2 global identification registers.  This is deliberately a
 * read-only preflight: the values determine the number of stream-match and
 * context-bank resources before the attach backend is allowed to program
 * any of them. */
#define GPU_SMMUV2_ID0              0x0020u
#define GPU_SMMUV2_ID1              0x0024u
#define GPU_SMMUV2_ID2              0x0028u

static void gpu_smmuv2_log_capabilities(void)
{
    if (!g_gpu_smmu_window.cpu_mapped)
    {
        terminal_warn("[K:GPU] GPU-local SMMUv2 window unavailable");
        return;
    }
    if (gpu_smmuv2_probe(&g_gpu_smmu_window, &g_gpu_smmu_caps) != 0)
    {
        terminal_warn("[K:GPU] GPU-local SMMUv2 capability read faulted; attach disabled");
        return;
    }
    terminal_print("[K:GPU] SMMUv2 ID0=");
    terminal_print_inline_hex64(g_gpu_smmu_caps.id0);
    terminal_print(" ID1=");
    terminal_print_inline_hex64(g_gpu_smmu_caps.id1);
    terminal_print(" ID2=");
    terminal_print_inline_hex64(g_gpu_smmu_caps.id2);
    terminal_print(" page-shift=");
    terminal_print_inline_hex64(g_gpu_smmu_caps.page_shift);
    terminal_print(" context-banks=");
    terminal_print_inline_hex64(g_gpu_smmu_caps.context_bank_count);
    terminal_print(" stream-groups=");
    terminal_print_inline_hex64(g_gpu_smmu_caps.stream_group_count);
    terminal_flush_log();
}

static int gpu_smmuv2_select_streams(const gpu_iommu_attach_plan *source,
                                     const uint32_t *stream_ids,
                                     uint32_t stream_count,
                                     gpu_iommu_attach_plan *out)
{
    if (!source || !stream_ids || !stream_count || !out ||
        stream_count > GPU_IOMMU_MAX_STREAM_IDS)
        return -1;
    *out = *source;
    out->stream_id_count = 0u;
    for (uint32_t wanted = 0u; wanted < stream_count; ++wanted)
    {
        uint32_t found = 0u;
        for (uint32_t available = 0u;
             available < source->stream_id_count;
             ++available)
        {
            if (source->stream_ids[available] == stream_ids[wanted])
            {
                found = 1u;
                break;
            }
        }
        if (!found)
            return -2;
        out->stream_ids[out->stream_id_count++] = stream_ids[wanted];
    }
    return 0;
}

static void gpu_smmuv2_log_stream_binding(uint32_t stream_id)
{
    gpu_smmuv2_stream_binding binding = {0};
    int rc = gpu_smmuv2_find_stream_binding(&g_gpu_smmu_window,
                                            &g_gpu_smmu_caps, stream_id,
                                            &binding);

    terminal_print(" [SID ");
    terminal_print_inline_hex64(stream_id);
    if (rc == 0)
    {
        terminal_print(" -> SMR ");
        terminal_print_inline_hex64(binding.smr_index);
        terminal_print(" S2CR=");
        terminal_print_inline_hex64(binding.s2cr);
    }
    else
        terminal_print(" -> no stream match]");
}

static void gpu_smmuv2_log_render_binding(void)
{
    gpu_smmuv2_context_state state = {0};

    if (gpu_smmuv2_read_context_state(&g_gpu_smmu_window,
                                      &g_gpu_smmu_caps,
                                      g_gpu_smmu_context_bank, &state) != 0)
        return;
    terminal_print("[K:GPU] SMMU CB0 state SCTLR=");
    terminal_print_inline_hex64(state.sctlr);
    terminal_print(" TCR2=");
    terminal_print_inline_hex64(state.tcr2);
    terminal_print(" TCR=");
    terminal_print_inline_hex64(state.tcr);
    terminal_print(" TTBR0=");
    terminal_print_inline_hex64(state.ttbr0);
    terminal_print(" MAIR0=");
    terminal_print_inline_hex64(state.mair0);
    for (uint32_t i = 0u; i < ADRENO_X1_85_RENDER_SID_COUNT; ++i)
        gpu_smmuv2_log_stream_binding(g_adreno_x1_85_render_sids[i]);
    terminal_flush_log();
}

static int gpu_smmuv2_attach_staged_domain(void *context)
{
    gpu_iommu_attach_plan render_plan;
    gpu_iommu_attach_plan gmu_plan;
    uint32_t render_bound = 0u;
    uint32_t gmu_bound = 0u;
    int rc;
    (void)context;

    terminal_print("[K:GPU] attaching staged render and GMU domains to GPU SMMUv2");
    terminal_flush_log();
    if (gpu_smmuv2_select_streams(&g_iommu_attach_plan,
                                  g_adreno_x1_85_render_sids,
                                  ADRENO_X1_85_RENDER_SID_COUNT,
                                  &render_plan) != 0 ||
        gpu_smmuv2_select_streams(&g_iommu_attach_plan,
                                  g_adreno_x1_85_gmu_sids,
                                  ADRENO_X1_85_GMU_SID_COUNT,
                                  &gmu_plan) != 0)
    {
        terminal_warn("[K:GPU] IORT lacks an X1E render or GMU stream");
        return -1;
    }

    rc = gpu_smmuv2_attach(&g_gpu_smmu_window, &g_gpu_smmu_caps,
                           &render_plan, &g_gpu_smmu_context_bank,
                           &render_bound);
    if (rc != 0 || render_bound != ADRENO_X1_85_RENDER_SID_COUNT)
    {
        terminal_warn("[K:GPU] render CB0 attach rejected or incomplete");
        return -2;
    }
    rc = gpu_smmuv2_attach_context(&g_gpu_smmu_window, &g_gpu_smmu_caps,
                                   &gmu_plan, 1u, &gmu_bound);
    if (rc != 0 || gmu_bound != ADRENO_X1_85_GMU_SID_COUNT)
    {
        terminal_warn("[K:GPU] GMU CB1 attach rejected or incomplete");
        return -3;
    }
    g_gmu_smmu_context_bank = 1u;
    g_gmu_smmu_bound_stream_count = gmu_bound;
    g_gpu_smmu_bound_stream_count = render_bound + gmu_bound;
    terminal_print("[K:GPU] SMMUv2 render CB=");
    terminal_print_inline_hex64(g_gpu_smmu_context_bank);
    terminal_print(" GMU CB=");
    terminal_print_inline_hex64(g_gmu_smmu_context_bank);
    terminal_print(" streams bound=");
    terminal_print_inline_hex64(g_gpu_smmu_bound_stream_count);
    terminal_flush_log();
    gpu_smmuv2_log_render_binding();
    return 0;
}

static int gpu_gmu_upload_staged_image(void *context)
{
    uint32_t reset_signature;
    uint32_t segments;
    uint32_t bytes;
    int rc;
    (void)context;

    if (!g_gmu_memory.hfi_queue_count || !g_gmu_memory.external_segments_loaded)
        return -1;
    terminal_print("[K:GPU] GMU DTCM preflight before firmware upload");
    terminal_flush_log();
    rc = gpu_gmu_tcm_preflight(&g_gpu_gmu_window, &reset_signature);
    if (rc != 0)
    {
        terminal_warn("[K:GPU] GMU DTCM preflight faulted; upload withheld");
        return -2;
    }
    terminal_print("[K:GPU] GMU DTCM reset signature=");
    terminal_print_inline_hex64(reset_signature);
    terminal_flush_log();
    rc = gpu_gmu_upload_tcm(&g_gpu_gmu_window, &g_gmu_image, &segments,
                            &bytes);
    if (rc != 0)
    {
        terminal_warn("[K:GPU] guarded GMU TCM upload failed");
        return -3;
    }
    terminal_print("[K:GPU] GMU TCM upload complete segments=");
    terminal_print_inline_hex64(segments);
    terminal_print(" bytes=");
    terminal_print_inline_hex64(bytes);
    terminal_flush_log();
    g_gmu_reset_signature = reset_signature;
    return 0;
}

static int gpu_gmu_start_staged_firmware(void *context)
{
    gpu_gmu_start_status status = {0};
    gpu_smmuv2_context_fault gmu_fault = {0};
    int prep_rc;
    int rc;
    (void)context;

    if (!g_gmu_reset_signature)
        return -1;
    terminal_print("[K:GPU] applying X1E CX/GMU cold-boot prerequisites");
    terminal_flush_log();
    prep_rc = adreno_x1_85_prepare_gmu_cold_boot(&g_gpu_regs_window,
                                                  &g_gpu_gmu_window,
                                                  &g_gpu_rscc_window,
                                                  &g_gpu_pdc_window);
    if (prep_rc != 0)
    {
        terminal_warn("[K:GPU] X1E CX/GMU cold-boot prerequisite failed");
        terminal_print("[K:GPU] CX/RSCC/PDC prerequisite rc=");
        terminal_print_inline_hex64((uint32_t)(-prep_rc));
        terminal_flush_log();
        return -2;
    }
    terminal_print("[K:GPU] releasing GMU reset and starting HFI control");
    terminal_flush_log();
    /* GX is still off so RBBM cannot provide a live chip-id.  The X1E profile
     * carries the GMU's firmware-facing ID instead; unlike the generic
     * fallback this Gen7 image requires its exact value during cold boot. */
    rc = gpu_gmu_start_cold(&g_gpu_gmu_window, &g_gmu_memory,
                            g_gmu_reset_signature,
                            ADRENO_X1_85_GMU_CHIP_ID, &status);
    if (rc == -3 && (status.fw_init_result & 0x1ffu) == 0x100u)
    {
        /* Firmware is demonstrably ready, so recover only the HFI latch in
         * this boot.  This avoids a second destructive CM3 reset when the
         * first read-window races the newly released firmware. */
        terminal_print("[K:GPU] retrying HFI control on ready GMU firmware");
        if (gpu_gmu_gen7_start_hfi_control(&g_gpu_gmu_window, &status) == 0)
            rc = 0;
    }
    if (rc != 0)
    {
        terminal_warn("[K:GPU] GMU cold start did not reach HFI-ready state");
        terminal_print("[K:GPU] GMU cold start rc=");
        terminal_print_inline_hex64((uint32_t)(-rc));
        terminal_print(" fw-init=");
        terminal_print_inline_hex64(status.fw_init_result);
        terminal_print(" hfi=");
        terminal_print_inline_hex64(status.hfi_control_status);
        terminal_print(" hfi-attempts=");
        terminal_print_inline_hex64(status.hfi_control_attempts);
        terminal_print(" breadcrumb=");
        terminal_print_inline_hex64(status.dtcm_breadcrumb);
        if (gpu_smmuv2_read_context_fault(&g_gpu_smmu_window,
                                          &g_gpu_smmu_caps,
                                          g_gmu_smmu_context_bank,
                                          &gmu_fault) == 0 &&
            (gmu_fault.fsr & GPU_SMMUV2_FSR_FAULT_MASK) != 0u)
        {
            terminal_print(" SMMU-CB1-FSR=");
            terminal_print_inline_hex64(gmu_fault.fsr);
            terminal_print(" FAR=");
            terminal_print_inline_hex64(gmu_fault.fault_address);
            terminal_print(" FSYNR0=");
            terminal_print_inline_hex64(gmu_fault.fsynr0);
            terminal_print(" FSYNR1=");
            terminal_print_inline_hex64(gmu_fault.fsynr1);
        }
        else
            terminal_print(" SMMU-CB1=no translation fault");
        terminal_flush_log();
        return -2;
    }
    terminal_print("[K:GPU] GMU firmware initialized result=");
    terminal_print_inline_hex64(status.fw_init_result);
    if (status.fw_init_confirmed_after_poll)
        terminal_print(" (confirmed after poll boundary)");
    if (status.fw_init_used_compatibility_bit)
        terminal_print(" (Gen7 compatibility bit accepted)");
    terminal_print(" HFI status=");
    terminal_print_inline_hex64(status.hfi_control_status);
    terminal_print(" attempts=");
    terminal_print_inline_hex64(status.hfi_control_attempts);
    terminal_flush_log();
    return 0;
}

/* The ACPI namespace already identifies this device as \_SB.GPU0.  Keep the
 * power call behind the generic ACPI executor rather than duplicating a
 * Windows PEP protocol or inventing RPMh resource values. */
static int gpu_acpi_power_on(uint64_t rsdp_phys)
{
    uint64_t sta = 0u;
    uint64_t ret = 0u;
    int sta_rc;
    int ps0_rc;

    if (!rsdp_phys)
        return -1;
    acpi_probe_net_exec_context_reset();
    sta_rc = acpi_probe_net_exec_device_method(rsdp_phys, "GPU0", "_STA",
                                               &sta);
    terminal_print("[K:GPU] ACPI GPU0._STA rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)sta_rc);
    terminal_print(" ret=");
    terminal_print_inline_hex64(sta);
    terminal_flush_log();
    ps0_rc = acpi_probe_net_exec_device_method(rsdp_phys, "GPU0", "_PS0",
                                               &ret);
    terminal_print("[K:GPU] ACPI GPU0._PS0 rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)ps0_rc);
    terminal_print(" ret=");
    terminal_print_inline_hex64(ret);
    terminal_flush_log();
    return ps0_rc;
}

/* X1E has no GPU0._PS0.  The documented GPUCC CX GDSC and its GMU clock
 * branches are consequently the first live power transition.  Stop the
 * generic plan before SMMU/firmware actions; those have their own backends. */
static int gpu_gpucc_baseline_vote(void *context)
{
    (void)context;
    terminal_print("[K:GPU] starting documented GPUCC CX/GMU clock sequence");
    terminal_flush_log();
    return 0;
}

static int gpu_bringup_stop_before_smmu(void *context)
{
    (void)context;
    terminal_print("[K:GPU] GPUCC sequence complete; stopping before SMMU attach");
    terminal_flush_log();
    return -1;
}

static int map_staged_firmware(void)
{
    uint64_t next_iova = GPU_CORE_STAGING_IOVA_BASE;

    gpu_iommu_domain_release(&g_iommu_domain);
    gpu_ring_release(&g_submission_ring);
    gpu_buffer_release(&g_cp_shadow);
    gpu_buffer_release(&g_cp_pwrup);
    if (gpu_iommu_domain_init(&g_iommu_domain, GPU_IOMMU_MAX_VA_BITS) != 0)
        return -1;
    for (uint32_t i = 0u; i < g_firmware.blob_count; ++i)
    {
        gpu_buffer *buffer = &g_firmware.blobs[i].buffer;
        if (gpu_iommu_map_buffer(&g_iommu_domain, buffer, next_iova) != 0)
        {
            gpu_iommu_domain_release(&g_iommu_domain);
            return -2;
        }
        next_iova += (buffer->pages << 12);
    }
    if (gpu_ring_init(&g_submission_ring, GPU_CORE_SUBMISSION_BYTES) != 0 ||
        gpu_iommu_map_buffer(&g_iommu_domain, &g_submission_ring.buffer,
                             GPU_CORE_SUBMISSION_IOVA) != 0 ||
        gpu_buffer_alloc(&g_cp_shadow, 0x1000u,
                         GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0 ||
        gpu_iommu_map_buffer(&g_iommu_domain, &g_cp_shadow,
                             GPU_CORE_CP_SHADOW_IOVA) != 0 ||
        gpu_buffer_alloc(&g_cp_pwrup, 0x1000u,
                         GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0 ||
        gpu_iommu_map_buffer(&g_iommu_domain, &g_cp_pwrup,
                             GPU_CORE_CP_PWRUP_IOVA) != 0)
    {
        gpu_ring_release(&g_submission_ring);
        gpu_buffer_release(&g_cp_shadow);
        gpu_buffer_release(&g_cp_pwrup);
        gpu_iommu_domain_release(&g_iommu_domain);
        return -3;
    }
    gpu_buffer_prepare_for_device(&g_cp_shadow);
    gpu_buffer_prepare_for_device(&g_cp_pwrup);
    if (g_scanout_target.buffer.cpu &&
        gpu_iommu_map_buffer(&g_iommu_domain, &g_scanout_target.buffer,
                             GPU_CORE_SCANOUT_IOVA) != 0)
    {
        gpu_ring_release(&g_submission_ring);
        gpu_buffer_release(&g_cp_shadow);
        gpu_buffer_release(&g_cp_pwrup);
        gpu_iommu_domain_release(&g_iommu_domain);
        return -4;
    }
    if (gpu_gmu_memory_map(&g_iommu_domain, &g_gmu_memory) != 0)
    {
        gpu_ring_release(&g_submission_ring);
        gpu_buffer_release(&g_cp_shadow);
        gpu_buffer_release(&g_cp_pwrup);
        gpu_iommu_domain_release(&g_iommu_domain);
        return -5;
    }
    gpu_iommu_domain_prepare_for_device(&g_iommu_domain);
    return 0;
}

int gpu_core_init(const boot_info *boot)
{
    adreno_x1_85_profile profile;
    int rc;

    g_primary = (gpu_device_info){0};
    g_iommu_topology = (gpu_iommu_topology){0};
    g_iommu_attach_plan = (gpu_iommu_attach_plan){0};
    g_gpu_smmu_caps = (gpu_smmuv2_caps){0};
    g_gpu_smmu_context_bank = 0u;
    g_gpu_smmu_bound_stream_count = 0u;
    g_gmu_smmu_context_bank = 0u;
    g_gmu_smmu_bound_stream_count = 0u;
    g_gmu_image = (gpu_gmu_image){0};
    g_gmu_reset_signature = 0u;
    gpu_gmu_memory_release(&g_gmu_memory);
    rpmh_cmd_db_mapping_release(&g_rpmh_cmd_db);
    gpu_buffer_release(&g_scanout_target.buffer);
    g_scanout_target = (gpu_scanout_target){0};
    gpu_iommu_domain_release(&g_iommu_domain);
    gpu_ring_release(&g_submission_ring);
    gpu_buffer_release(&g_cp_shadow);
    gpu_buffer_release(&g_cp_pwrup);
    gpu_firmware_release(&g_firmware);
    gpu_mmio_unmap_window(&g_gpu_regs_window);
    gpu_mmio_unmap_window(&g_gpu_pdc_window);
    gpu_mmio_unmap_window(&g_gpu_smmu_window);
    gpu_mmio_unmap_window(&g_gpu_gmu_window);
    gpu_mmio_unmap_window(&g_gpu_rscc_window);
    if (boot && boot->fb.fb_base && boot->fb.fb_size &&
        gpu_buffer_import(&g_scanout_target.buffer,
                          (void *)(uintptr_t)boot->fb.fb_base,
                          boot->fb.fb_base, boot->fb.fb_size,
                          GPU_BUFFER_DATA) == 0)
    {
        g_scanout_target.width = boot->fb.width;
        g_scanout_target.height = boot->fb.height;
        g_scanout_target.pitch = boot->fb.pitch;
        g_scanout_target.pixel_format = boot->fb.pixel_format;
    }
    else if (boot && boot->fb.fb_base)
        terminal_warn("[K:GPU] UEFI scanout cannot be used as a GPU target");
    rc = adreno_x1_85_probe_profile(boot ? boot->acpi_rsdp : 0u, &profile);
    if (rc != 0)
        return rc;

    g_primary.state = GPU_DEVICE_DISCOVERED;
    g_primary.regs_base = profile.gfx_regs_base;
    g_primary.regs_size = profile.gfx_regs_size;
    g_primary.primary_irq = profile.gfx_spi;
    /* IORT topology is resolved below; isolation starts only once an attach
     * backend has configured the discovered SMMU endpoint(s). */
    g_primary.caps.max_gpu_va_bits = 0u;
    g_primary.caps.flags = 0u;
    if (gpu_mmio_map_window(&g_gpu_regs_window,
                            profile.gfx_regs_base,
                            profile.gfx_regs_size) == 0 &&
        gpu_mmio_map_window(&g_gpu_pdc_window,
                            profile.pdc_regs_base,
                            ADRENO_X1_85_PDC_MMIO_SIZE) == 0 &&
        gpu_mmio_map_window(&g_gpu_smmu_window,
                            profile.gpu_smmu_base,
                            ADRENO_X1_85_GPU_SMMU_SIZE) == 0)
    {
        g_primary.mmio_cpu_mapped = 1u;
        terminal_print("[K:GPU] CPU MMIO windows mapped; peripheral I/O guarded");
    }
    else
        terminal_warn("[K:GPU] CPU MMIO mapping failed; GPU remains inert");
    if (g_primary.mmio_cpu_mapped)
    {
        uint32_t hw_version = 0u;
        uint32_t rbbm_status = 0u;
        int power_rc = gpu_acpi_power_on(boot ? boot->acpi_rsdp : 0u);
        int version_rc = gpu_mmio_try_read32(&g_gpu_regs_window,
                                             ADRENO_X1_85_RBBM_HW_VERSION,
                                             &hw_version);
        int status_rc = gpu_mmio_try_read32(&g_gpu_regs_window,
                                            ADRENO_X1_85_RBBM_STATUS,
                                            &rbbm_status);

        if (power_rc != 0)
            terminal_warn("[K:GPU] ACPI GPU power method unavailable or failed");
        if (version_rc == 0)
        {
            terminal_print("[K:GPU] preflight RBBM HW version=");
            terminal_print_inline_hex64(hw_version);
        }
        else
            terminal_warn("[K:GPU] preflight RBBM HW-version read faulted");
        if (status_rc == 0)
        {
            terminal_print("[K:GPU] preflight RBBM status=");
            terminal_print_inline_hex64(rbbm_status);
        }
        else
            terminal_warn("[K:GPU] preflight RBBM-status read faulted");
        terminal_flush_log();
    }
    {
        adreno_x1_85_block_map blocks;
        if (adreno_x1_85_resolve_blocks(&profile, &blocks) == 0)
        {
            gpu_bringup_target_window windows[GPU_BRINGUP_TARGET_COUNT] = {
                [GPU_BRINGUP_TARGET_GPUCC] = {blocks.gpucc_size,
                                               g_gpu_regs_window.cpu_base +
                                                   (blocks.gpucc_base - g_gpu_regs_window.phys_base),
                                               g_gpu_regs_window.cpu_mapped},
                [GPU_BRINGUP_TARGET_RSCC] = {blocks.rscc_size,
                                              g_gpu_regs_window.cpu_base +
                                                  (blocks.rscc_base - g_gpu_regs_window.phys_base),
                                              g_gpu_regs_window.cpu_mapped},
                [GPU_BRINGUP_TARGET_GMU] = {blocks.gmu_size,
                                             g_gpu_regs_window.cpu_base +
                                                 (blocks.gmu_base - g_gpu_regs_window.phys_base),
                                             g_gpu_regs_window.cpu_mapped},
            };
            g_gpu_gmu_window = (gpu_mmio_window){blocks.gmu_base,
                blocks.gmu_size, g_gpu_regs_window.cpu_base +
                    (blocks.gmu_base - g_gpu_regs_window.phys_base),
                g_gpu_regs_window.cpu_mapped};
            g_gpu_rscc_window = (gpu_mmio_window){blocks.rscc_base,
                blocks.rscc_size, g_gpu_regs_window.cpu_base +
                    (blocks.rscc_base - g_gpu_regs_window.phys_base),
                g_gpu_regs_window.cpu_mapped};
            terminal_print("[K:GPU] X1E subblocks: RSCC/GMU/GPUCC validated");
            terminal_print("[K:GPU] GMU base=");
            terminal_print_inline_hex64(blocks.gmu_base);
            terminal_print("[K:GPU] GPUCC base=");
            terminal_print_inline_hex64(blocks.gpucc_base);
            if (gpu_bringup_validate(adreno_x1_85_bringup_plan(), windows) == 0)
            {
                gpu_bringup_backend backend = {
                    0, gpu_gpucc_baseline_vote, gpu_bringup_stop_before_smmu,
                    0, 0, 100000u
                };
                int bringup_rc;
                terminal_print("[K:GPU] guarded bring-up plan validated");
                bringup_rc = gpu_bringup_execute(adreno_x1_85_bringup_plan(),
                                                  windows, &backend);
                if (bringup_rc == -13)
                    terminal_print("[K:GPU] GPUCC CX/GMU fabric enabled");
                else
                {
                    terminal_warn("[K:GPU] GPUCC CX/GMU sequence failed");
                    terminal_print("[K:GPU] GPUCC sequence rc=");
                    terminal_print_inline_hex64((uint32_t)(-bringup_rc));
                }
                {
                    uint32_t live_hw_version = 0u;
                    if (gpu_mmio_try_read32(&g_gpu_regs_window,
                                            ADRENO_X1_85_RBBM_HW_VERSION,
                                            &live_hw_version) == 0)
                    {
                        terminal_print("[K:GPU] post-GPUCC RBBM HW version=");
                        terminal_print_inline_hex64(live_hw_version);
                    }
                    else
                        terminal_warn("[K:GPU] post-GPUCC RBBM read faulted");
                    terminal_flush_log();
                }
            }
            else
                terminal_warn("[K:GPU] bring-up plan rejected; GPU remains inert");
        }
        else
            terminal_warn("[K:GPU] X1E subblock map is outside ACPI aperture");
    }
    {
        int iommu_rc = adreno_x1_85_resolve_iommu(boot ? boot->acpi_rsdp : 0u,
                                                   &g_iommu_topology);
        uint32_t has_gpu_smmuv2 = 0u;

        if (iommu_rc == 0)
        {
            terminal_print("[K:GPU] IORT GPU0 IOMMU endpoints=");
            terminal_print_inline_hex64(g_iommu_topology.endpoint_count);
            for (uint32_t i = 0u; i < g_iommu_topology.endpoint_count; ++i)
            {
                const gpu_iommu_endpoint *endpoint =
                    &g_iommu_topology.endpoints[i];
                terminal_print("[K:GPU] IORT endpoint type=");
                terminal_print_inline_hex64(endpoint->architecture);
                terminal_print("[K:GPU] IORT endpoint base=");
                terminal_print_inline_hex64(endpoint->base);
                terminal_print("[K:GPU] IORT endpoint stream IDs=");
                terminal_print_inline_hex64(endpoint->stream_id_count);
                for (uint32_t stream = 0u;
                     stream < endpoint->stream_id_count;
                     ++stream)
                {
                    terminal_print("[K:GPU] IORT stream ID=");
                    terminal_print_inline_hex64(endpoint->stream_ids[stream]);
                }
                if (endpoint->architecture == GPU_IOMMU_ARCH_SMMU_V1V2 &&
                    endpoint->base == profile.gpu_smmu_base)
                    has_gpu_smmuv2 = 1u;
            }
            if (has_gpu_smmuv2)
            {
                terminal_print("[K:GPU] GPU-local SMMUv2 binding verified");
                gpu_smmuv2_log_capabilities();
            }
            else
                terminal_warn("[K:GPU] IORT lacks the expected GPU-local v2 endpoint; attach disabled");
        }
        else
        {
            terminal_warn("[K:GPU] IORT GPU0 binding unresolved; domain disabled");
            terminal_print("[K:GPU] IORT binding failure code=");
            terminal_print_inline_hex64((uint32_t)(iommu_rc < 0 ? -iommu_rc :
                                                               iommu_rc));
        }
    }
    {
        gpu_firmware_inventory firmware;
        int firmware_rc = gpu_firmware_scan(adreno_x1_85_firmware_manifest(), &firmware);

        g_primary.firmware_required = firmware.required_count;
        g_primary.firmware_present = firmware.present_count;
        terminal_print("[K:GPU] firmware staged required=");
        terminal_print_inline_hex64(firmware.required_count);
        terminal_print("[K:GPU] firmware staged present=");
        terminal_print_inline_hex64(firmware.present_count);
        if (firmware_rc == 0 && gpu_firmware_load(adreno_x1_85_firmware_manifest(), &g_firmware) == 0)
        {
            terminal_print("[K:GPU] firmware loaded bytes=");
            terminal_print_inline_hex64(g_firmware.total_bytes);
            if (gpu_gmu_image_parse(&g_firmware, &g_gmu_image) == 0)
            {
                terminal_print("[K:GPU] Gen7 GMU image segments=");
                terminal_print_inline_hex64(g_gmu_image.segment_count);
                terminal_print(" itcm-bytes=");
                terminal_print_inline_hex64(g_gmu_image.itcm_bytes);
                terminal_print(" dtcm-bytes=");
                terminal_print_inline_hex64(g_gmu_image.dtcm_bytes);
                terminal_print(" external-bytes=");
                terminal_print_inline_hex64(g_gmu_image.external_bytes);
                if (gpu_gmu_memory_stage(&g_gmu_image, &g_gmu_memory) == 0)
                {
                    terminal_print("[K:GPU] GMU external image staged segments=");
                    terminal_print_inline_hex64(g_gmu_memory.external_segments_loaded);
                    terminal_print(" hfi-iova=");
                    terminal_print_inline_hex64(GPU_GMU_HFI_IOVA);
                    terminal_print(" hfi-queues=");
                    terminal_print_inline_hex64(g_gmu_memory.hfi_queue_count);
                }
                else
                    terminal_warn("[K:GPU] unable to stage required GMU external memory");
            }
            else
                terminal_warn("[K:GPU] Gen7 GMU image rejected before upload");
            if (map_staged_firmware() == 0)
            {
                terminal_print("[K:GPU] staged DMA domain root=");
                terminal_print_inline_hex64(g_iommu_domain.root_phys);
                terminal_print("[K:GPU] staged DMA mappings=");
                terminal_print_inline_hex64(g_iommu_domain.mapping_count);
                terminal_print("[K:GPU] staged command ring IOVA=");
                terminal_print_inline_hex64(g_submission_ring.buffer.iova);
                terminal_print(" dwords=");
                terminal_print_inline_hex64(g_submission_ring.write_dwords);
                terminal_print("[K:GPU] staged CP shadow IOVA=");
                terminal_print_inline_hex64(g_cp_shadow.iova);
                terminal_print(" pwrup IOVA=");
                terminal_print_inline_hex64(g_cp_pwrup.iova);
                if (g_scanout_target.buffer.iova)
                {
                    terminal_print("[K:GPU] staged scanout IOVA=");
                    terminal_print_inline_hex64(g_scanout_target.buffer.iova);
                }
                if (gpu_iommu_build_attach_plan(&g_iommu_topology,
                                                 profile.gpu_smmu_base,
                                                 GPU_IOMMU_ARCH_SMMU_V1V2,
                                                 &g_iommu_domain,
                                                 &g_iommu_attach_plan) == 0)
                {
                    terminal_print("[K:GPU] SMMUv2 attach plan endpoint=");
                    terminal_print_inline_hex64(g_iommu_attach_plan.endpoint_base);
                    terminal_print("[K:GPU] SMMUv2 attach plan streams=");
                    terminal_print_inline_hex64(g_iommu_attach_plan.stream_id_count);
                    if (g_gpu_smmu_caps.context_bank_count)
                    {
                        adreno_x1_85_block_map attach_blocks;
                        gpu_bringup_target_window attach_windows[
                            GPU_BRINGUP_TARGET_COUNT];
                        gpu_bringup_backend backend = {
                            0, gpu_gpucc_baseline_vote,
                            gpu_smmuv2_attach_staged_domain,
                            gpu_gmu_upload_staged_image,
                            gpu_gmu_start_staged_firmware, 100000u
                        };
                        int bringup_rc;
                        if (adreno_x1_85_resolve_blocks(&profile,
                                                        &attach_blocks) != 0)
                        {
                            terminal_warn("[K:GPU] attach-stage block map invalid");
                            goto smmu_attach_done;
                        }
                        attach_windows[GPU_BRINGUP_TARGET_GPUCC] =
                            (gpu_bringup_target_window){attach_blocks.gpucc_size,
                                g_gpu_regs_window.cpu_base +
                                    (attach_blocks.gpucc_base -
                                     g_gpu_regs_window.phys_base),
                                g_gpu_regs_window.cpu_mapped};
                        attach_windows[GPU_BRINGUP_TARGET_RSCC] =
                            (gpu_bringup_target_window){attach_blocks.rscc_size,
                                g_gpu_regs_window.cpu_base +
                                    (attach_blocks.rscc_base -
                                     g_gpu_regs_window.phys_base),
                                g_gpu_regs_window.cpu_mapped};
                        attach_windows[GPU_BRINGUP_TARGET_GMU] =
                            (gpu_bringup_target_window){attach_blocks.gmu_size,
                                g_gpu_regs_window.cpu_base +
                                    (attach_blocks.gmu_base -
                                     g_gpu_regs_window.phys_base),
                                g_gpu_regs_window.cpu_mapped};
                        g_gpu_gmu_window = (gpu_mmio_window){
                            attach_blocks.gmu_base, attach_blocks.gmu_size,
                            attach_windows[GPU_BRINGUP_TARGET_GMU].cpu_base,
                            attach_windows[GPU_BRINGUP_TARGET_GMU].cpu_mapped};
                        g_gpu_rscc_window = (gpu_mmio_window){
                            attach_blocks.rscc_base, attach_blocks.rscc_size,
                            attach_windows[GPU_BRINGUP_TARGET_RSCC].cpu_base,
                            attach_windows[GPU_BRINGUP_TARGET_RSCC].cpu_mapped};
                        terminal_print("[K:GPU] executing guarded SMMUv2 attach stage");
                        bringup_rc = gpu_bringup_execute(
                            adreno_x1_85_bringup_plan(), attach_windows, &backend);
                        /* A null backend is the explicit, guarded stop for
                         * the next unimplemented action.  Reaching either
                         * stop therefore proves every earlier stage, rather
                         * than reporting the already-completed SMMU attach
                         * as a failure. */
                        if (bringup_rc == 0 || bringup_rc == -14 ||
                            bringup_rc == -16)
                        {
                            g_primary.caps.max_gpu_va_bits =
                                g_iommu_attach_plan.va_bits;
                            if (g_gpu_smmu_bound_stream_count ==
                                ADRENO_X1_85_DRIVER_SID_COUNT)
                            {
                                g_primary.caps.flags |= GPU_CAP_DMA_ISOLATED;
                                terminal_print("[K:GPU] staged DMA domain attached; GMU upload next");
                            }
                            else
                                terminal_warn("[K:GPU] partial SMMU attach; GMU bring-up allowed, DMA isolation pending");
                            if (bringup_rc == -16)
                                terminal_print("[K:GPU] GMU TCM firmware verified; reset remains held pending RPMh/HFI start");
                            else if (bringup_rc == 0)
                            {
                                int db_rc = rpmh_cmd_db_probe_firmware(
                                    boot, ADRENO_X1_85_CMD_DB_BASE,
                                    ADRENO_X1_85_CMD_DB_SIZE,
                                    &g_rpmh_cmd_db);
                                if (db_rc == 0)
                                {
                                    int hfi_rc = adreno_x1_85_build_hfi_perf_table(
                                        &g_rpmh_cmd_db.db, &g_hfi_perf_table);

                                    terminal_print("[K:GPU] X1E RPMh command DB validated base=");
                                    terminal_print_inline_hex64(
                                        g_rpmh_cmd_db.physical_base);
                                    if (hfi_rc == 0)
                                        hfi_rc = gpu_gmu_hfi_gen7_send_perf_table(
                                            &g_gpu_gmu_window, &g_gmu_memory,
                                            &g_hfi_perf_table);
                                    if (hfi_rc == 0)
                                    {
                                        int bw_rc = adreno_x1_85_build_hfi_bw_table(
                                            &g_rpmh_cmd_db.db, &g_hfi_bw_table);
                                        terminal_print("[K:GPU] Gen7 HFI performance table accepted");
                                        if (bw_rc == 0)
                                            bw_rc = gpu_gmu_hfi_gen7_send_bw_table(
                                                &g_gpu_gmu_window, &g_gmu_memory,
                                                &g_hfi_bw_table);
                                        if (bw_rc == 0)
                                        {
                                            int start_rc = gpu_gmu_hfi_gen7_send_core_fw_start(
                                                &g_gpu_gmu_window, &g_gmu_memory);
                                            terminal_print("[K:GPU] Gen7 HFI bandwidth table accepted");
                                            if (start_rc == 0)
                                                start_rc = gpu_gmu_hfi_gen7_send_start(
                                                    &g_gpu_gmu_window, &g_gmu_memory);
                                            if (start_rc == 0)
                                                start_rc = gpu_gmu_hfi_gen7_set_gx_bw(
                                                    &g_gpu_gmu_window, &g_gmu_memory,
                                                    g_hfi_perf_table.gx_level_count - 1u,
                                                    g_hfi_bw_table.level_count - 1u);
                                            if (start_rc == 0)
                                                terminal_print("[K:GPU] Gen7 HFI core, GMU start and GX vote accepted");
                                            else
                                                terminal_warn("[K:GPU] Gen7 HFI core start rejected");
                                            if (start_rc == 0)
                                            {
                                                uint32_t gx_ack = 0u;
                                                uint32_t gx_hw_version = 0u;
                                                gpu_cp_snapshot cp = {0};
                                                const gpu_firmware_blob *sqe;
                                                int gx_rc = gpu_gmu_gen7_acquire_gpu(
                                                    &g_gpu_gmu_window, &gx_ack);

                                                if (gx_rc == 0)
                                                {
                                                    if (gpu_mmio_try_read32(
                                                            &g_gpu_regs_window,
                                                            ADRENO_X1_85_RBBM_HW_VERSION,
                                                            &gx_hw_version) == 0)
                                                    {
                                                        terminal_print("[K:GPU] GX power lease acknowledged status=");
                                                        terminal_print_inline_hex64(gx_ack);
                                                        terminal_print("[K:GPU] GX live RBBM HW version=");
                                                        terminal_print_inline_hex64(gx_hw_version);
                                                        if (gpu_cp_snapshot_read(
                                                                &g_gpu_regs_window,
                                                                adreno_x1_85_cp_layout(),
                                                                &cp) == 0)
                                                        {
                                                            terminal_print("[K:GPU] GX live CP rptr=");
                                                            terminal_print_inline_hex64(cp.rb_rptr);
                                                            terminal_print(" wptr=");
                                                            terminal_print_inline_hex64(cp.rb_wptr);
                                                            terminal_print(" sqe=");
                                                            terminal_print_inline_hex64(cp.sqe_control);
                                                            terminal_print(" hw-fault=");
                                                            terminal_print_inline_hex64(cp.hw_fault);
                                                            terminal_print(" protect=");
                                                            terminal_print_inline_hex64(cp.protect_status);
                                                        }
                                                        else
                                                            terminal_warn("[K:GPU] CP live-register probe faulted");

                                                        sqe = gpu_firmware_find(
                                                            &g_firmware,
                                                            GPU_FIRMWARE_SQE);
                                                        if (!sqe || !sqe->buffer.iova)
                                                            terminal_warn("[K:GPU] SQE firmware IOVA unavailable; CP command withheld");
                                                        else if (cp.hw_fault || cp.protect_status)
                                                            terminal_warn("[K:GPU] CP reports a pre-existing fault; CP command withheld");
                                                        else
                                                        {
                                                            gpu_cp_bind_config cp_config = {
                                                                gpu_firmware_payload_iova(sqe),
                                                                g_submission_ring.buffer.iova,
                                                                g_cp_shadow.iova + GPU_CORE_CP_BR_RPTR_OFFSET,
                                                                g_cp_shadow.iova + GPU_CORE_CP_BV_RPTR_OFFSET,
                                                                (uint32_t)g_submission_ring.buffer.size_bytes,
                                                                ADRENO_X1_85_CP_RB_CNTL_BOOT,
                                                                /* A7xx does not use the legacy CP address-mode
                                                                 * register.  The SMMU remains 64-bit; writing
                                                                 * CP_ADDR_MODE_CNTL here hits a reserved register
                                                                 * on X1-85 (readback: DEAFBEAD). */
                                                                0u,
                                                                ADRENO_X1_85_CP_BR_APRIV_MASK,
                                                                ADRENO_X1_85_CP_AUX_APRIV_MASK,
                                                                ADRENO_X1_85_CP_AUX_APRIV_MASK,
                                                            };
                                                            int cp_rc;
                                                            uint64_t scm_available = 0u;
                                                            uint64_t scm_status = 0u;
                                                            uint64_t scm_esr = 0u;

                                                            cp_rc = gpu_qcom_scm_open_gpu_smmu_aperture(
                                                                g_gpu_smmu_context_bank,
                                                                &scm_available, &scm_status,
                                                                &scm_esr);
                                                            terminal_print("[K:GPU] Qualcomm CB0 aperture rc=");
                                                            terminal_print_inline_hex64(
                                                                (uint64_t)(uint32_t)(-cp_rc));
                                                            terminal_print(" available=");
                                                            terminal_print_inline_hex64(scm_available);
                                                            terminal_print(" status=");
                                                            terminal_print_inline_hex64(scm_status);
                                                            if (scm_esr)
                                                            {
                                                                terminal_print(" ESR=");
                                                                terminal_print_inline_hex64(scm_esr);
                                                            }
                                                            terminal_flush_log();
                                                            if (cp_rc != 0)
                                                            {
                                                                terminal_warn("[K:GPU] CB0 aperture service did not complete; continuing with the existing SMMUv2 mapping");
                                                                terminal_flush_log();
                                                            }
                                                            cp_rc = adreno_x1_85_prepare_cp_host(
                                                                &g_gpu_regs_window);
                                                            terminal_print("[K:GPU] applying Gen7 host CP setup");
                                                            terminal_flush_log();
                                                            if (cp_rc != 0)
                                                            {
                                                                terminal_warn("[K:GPU] Gen7 host CP setup failed; CP command withheld");
                                                                terminal_print_inline_hex64((uint64_t)(uint32_t)(-cp_rc));
                                                                terminal_flush_log();
                                                            }
                                                            else
                                                            {
                                                            cp_rc = adreno_x1_85_build_cp_pwrup_record(
                                                                &g_gpu_regs_window, &g_cp_pwrup);
                                                            if (cp_rc == 0)
                                                                cp_rc = adreno_x1_85_emit_minimal_cp_init(
                                                                    &g_submission_ring,
                                                                    g_cp_pwrup.iova);
                                                            if (cp_rc == 0)
                                                                cp_rc = gpu_ring_seal(
                                                                    &g_submission_ring);
                                                            if (cp_rc != 0)
                                                            {
                                                                terminal_warn("[K:GPU] Gen7 CP power-up record or init ring failed; CP command withheld");
                                                                terminal_print_inline_hex64(
                                                                    (uint64_t)(uint32_t)(-cp_rc));
                                                                terminal_flush_log();
                                                            }
                                                            if (cp_rc == 0)
                                                            {
                                                            terminal_print("[K:GPU] binding Gen7 SQE IOVA=");
                                                            terminal_print_inline_hex64(cp_config.sqe_iova);
                                                            terminal_print(" ring IOVA=");
                                                            terminal_print_inline_hex64(cp_config.ring_iova);
                                                            terminal_flush_log();
                                                            cp_rc = gpu_cp_bind(
                                                                &g_gpu_regs_window,
                                                                adreno_x1_85_cp_layout(),
                                                                &cp_config);
                                                            if (cp_rc == 0 &&
                                                                gpu_cp_snapshot_read(
                                                                    &g_gpu_regs_window,
                                                                    adreno_x1_85_cp_layout(),
                                                                    &cp) == 0)
                                                            {
                                                                terminal_print("[K:GPU] CP bind readback SQE=");
                                                                terminal_print_inline_hex64(cp.sqe_instruction_base);
                                                                terminal_print(" ring=");
                                                                terminal_print_inline_hex64(cp.rb_base);
                                                                terminal_print(" BR-shadow=");
                                                                terminal_print_inline_hex64(cp.rb_rptr_address);
                                                                terminal_print(" BV-shadow=");
                                                                terminal_print_inline_hex64(cp.bv_rb_rptr_address);
                                                                terminal_print(" rb-cntl=");
                                                                terminal_print_inline_hex64(cp.rb_control);
                                                                terminal_print(" addr-mode=");
                                                                terminal_print_inline_hex64(cp.address_mode_control);
                                                                terminal_print(" apriv=");
                                                                terminal_print_inline_hex64(cp.apriv_control);
                                                                terminal_flush_log();
                                                            }
                                                            if (cp_rc == 0)
                                                            {
                                                                terminal_print("[K:GPU] submitting minimal Gen7 CP init");
                                                                terminal_flush_log();
                                                                cp_rc = gpu_cp_submit_fenced(
                                                                    &g_gpu_regs_window,
                                                                    &g_gpu_gmu_window,
                                                                    ADRENO_X1_85_GMU_AHB_FENCE_STATUS,
                                                                    adreno_x1_85_cp_layout(),
                                                                    &g_submission_ring);
                                                            }
                                                            if (cp_rc == 0)
                                                                cp_rc = gpu_cp_wait_ring(
                                                                    &g_gpu_regs_window,
                                                                    adreno_x1_85_cp_layout(),
                                                                    g_submission_ring.write_dwords,
                                                                    100000u, &cp);
                                                            if (cp_rc == 0 && !cp.hw_fault &&
                                                                !cp.protect_status)
                                                            {
                                                                terminal_print("[K:GPU] CP minimal init consumed rptr=");
                                                                terminal_print_inline_hex64(cp.rb_rptr);
                                                                terminal_print(" wptr=");
                                                                terminal_print_inline_hex64(cp.rb_wptr);
                                                                terminal_print(" sqe=");
                                                                terminal_print_inline_hex64(cp.sqe_control);
                                                            }
                                                            else
                                                            {
                                                                terminal_warn("[K:GPU] CP minimal init did not complete cleanly");
                                                                terminal_print("[K:GPU] CP init rc=");
                                                                terminal_print_inline_hex64(
                                                                    (uint32_t)(-cp_rc));
                                                                terminal_print(" rptr=");
                                                                terminal_print_inline_hex64(cp.rb_rptr);
                                                                terminal_print(" wptr=");
                                                                terminal_print_inline_hex64(cp.rb_wptr);
                                                                terminal_print(" fault=");
                                                                terminal_print_inline_hex64(cp.hw_fault);
                                                                terminal_print(" protect=");
                                                                terminal_print_inline_hex64(cp.protect_status);
                                                                terminal_print(" sqe=");
                                                                terminal_print_inline_hex64(cp.sqe_control);
                                                                {
                                                                    uint32_t rbbm_interrupt = 0u;
                                                                    uint32_t cp_interrupt = 0u;
                                                                    uint32_t cp_to_gmu = 0u;
                                                                    uint32_t roq_ring = 0u;
                                                                    gpu_smmuv2_context_fault render_fault = {0};
                                                                    gpu_smmuv2_global_fault global_fault = {0};

                                                                    if (gpu_mmio_try_read32(
                                                                            &g_gpu_regs_window,
                                                                            ADRENO_X1_85_RBBM_INT_STATUS,
                                                                            &rbbm_interrupt) == 0 &&
                                                                        gpu_mmio_try_read32(
                                                                            &g_gpu_regs_window,
                                                                            ADRENO_X1_85_CP_INTERRUPT_STATUS,
                                                                            &cp_interrupt) == 0 &&
                                                                        gpu_mmio_try_read32(
                                                                            &g_gpu_regs_window,
                                                                            ADRENO_X1_85_CP_CP2GMU_STATUS,
                                                                            &cp_to_gmu) == 0 &&
                                                                        gpu_mmio_try_read32(
                                                                            &g_gpu_regs_window,
                                                                            ADRENO_X1_85_CP_ROQ_RB_STATUS,
                                                                            &roq_ring) == 0)
                                                                    {
                                                                        terminal_print(" RBBM-irq=");
                                                                        terminal_print_inline_hex64(rbbm_interrupt);
                                                                        terminal_print(" CP-irq=");
                                                                        terminal_print_inline_hex64(cp_interrupt);
                                                                        terminal_print(" CP2GMU=");
                                                                        terminal_print_inline_hex64(cp_to_gmu);
                                                                        terminal_print(" ROQ-RB=");
                                                                        terminal_print_inline_hex64(roq_ring);
                                                                    }

                                                                    if (gpu_smmuv2_read_context_fault(
                                                                            &g_gpu_smmu_window,
                                                                            &g_gpu_smmu_caps,
                                                                            g_gpu_smmu_context_bank,
                                                                            &render_fault) == 0 &&
                                                                        (render_fault.fsr &
                                                                         GPU_SMMUV2_FSR_FAULT_MASK) != 0u)
                                                                    {
                                                                        terminal_print(" SMMU-CB0-FSR=");
                                                                        terminal_print_inline_hex64(render_fault.fsr);
                                                                        terminal_print(" FAR=");
                                                                        terminal_print_inline_hex64(
                                                                            render_fault.fault_address);
                                                                        terminal_print(" FSYNR0=");
                                                                        terminal_print_inline_hex64(
                                                                            render_fault.fsynr0);
                                                                        terminal_print(" FSYNR1=");
                                                                        terminal_print_inline_hex64(
                                                                            render_fault.fsynr1);
                                                                    }
                                                                    else
                                                                        terminal_print(
                                                                            " SMMU-CB0=no translation fault");
                                                                    if (gpu_smmuv2_read_global_fault(
                                                                            &g_gpu_smmu_window,
                                                                            &global_fault) == 0 &&
                                                                        global_fault.gfsr != 0u)
                                                                    {
                                                                        terminal_print(" SMMU-global-FSR=");
                                                                        terminal_print_inline_hex64(
                                                                            global_fault.gfsr);
                                                                        terminal_print(" SYNR0=");
                                                                        terminal_print_inline_hex64(
                                                                            global_fault.gfsynr0);
                                                                        terminal_print(" SYNR1=");
                                                                        terminal_print_inline_hex64(
                                                                            global_fault.gfsynr1);
                                                                        terminal_print(" SYNR2=");
                                                                        terminal_print_inline_hex64(
                                                                            global_fault.gfsynr2);
                                                                    }
                                                                    else
                                                                        terminal_print(
                                                                            " SMMU-global=no unmatched-stream fault");
                                                            }
                                                            }
                                                        }
                                                            terminal_flush_log();
                                                        }
                                                    }
                                                }
                                                    else
                                                        terminal_warn("[K:GPU] GX RBBM read failed while lease held");
                                                    if (gpu_gmu_gen7_release_gpu(
                                                            &g_gpu_gmu_window) != 0)
                                                        terminal_warn("[K:GPU] GX power lease release failed");
                                                }
                                                else
                                                    terminal_warn("[K:GPU] GX power lease was not acknowledged");
                                            }
                                        }
                                        else
                                            terminal_warn("[K:GPU] Gen7 HFI bandwidth table rejected");
                                    }
                                    else
                                        terminal_warn("[K:GPU] Gen7 HFI performance table rejected");
                                }
                                else
                                {
                                    terminal_print("[K:GPU] GX awaits firmware command DB candidate rc=");
                                    terminal_print_inline_hex64(
                                        (uint32_t)(-db_rc));
                                }
                            }
                        }
                        else if (bringup_rc == -17)
                            terminal_warn("[K:GPU] GMU start stage failed after SMMUv2 attach");
                        else
                            terminal_warn("[K:GPU] guarded SMMUv2 attach stage failed");
                    }
                    else
                        terminal_warn("[K:GPU] SMMUv2 capabilities unavailable; attach skipped");
smmu_attach_done:
                    ;
                }
                else
                    terminal_warn("[K:GPU] no valid GPU-local SMMUv2 attach plan");
            }
            else
                terminal_warn("[K:GPU] unable to build staged DMA domain");
        }
        else
            terminal_print("[K:GPU] firmware unavailable or invalid; GPU remains disabled");
    }

    /* GPU enablement eventually crosses a firmware-managed power boundary.
     * Preserve the complete discovery record before a later backend is
     * permitted to execute a hardware plan. */
    terminal_flush_log();
    return 0;
}

const gpu_device_info *gpu_core_primary(void)
{
    return &g_primary;
}

const gpu_firmware_set *gpu_core_firmware(void)
{
    return &g_firmware;
}

const gpu_iommu_topology *gpu_core_iommu_topology(void)
{
    return &g_iommu_topology;
}

const gpu_scanout_target *gpu_core_scanout_target(void)
{
    return &g_scanout_target;
}
