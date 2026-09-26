#include "gpu/gpu_core.h"
#include "gpu/adreno_x1_85.h"
#include "gpu/adreno_x1_85_3d.h"
#include "gpu/gpu_firmware.h"
#include "gpu/gpu_gmu_boot.h"
#include "gpu/gpu_gmu_image.h"
#include "gpu/gpu_gmu_memory.h"
#include "gpu/gpu_cp.h"
#include "gpu/gpu_iommu.h"
#include "gpu/gpu_mmio.h"
#include "gpu/gpu_qcom_scm.h"
#include "gpu/gpu_zap.h"
#include "gpu/gpu_smmu_v2.h"
#include "gpu/gpu_ring.h"
#include "gpu/gpu_render.h"
#include "gpu/gpu_render_backend.h"
#include "gpu/rpmh_cmd_db.h"
#include "asm/asm.h"
#include "hardware_probes/acpi_probe_net_candidates.h"
#include "mesart_shader_blob.h"
#include "system/dihos_time.h"
#include "terminal/terminal_api.h"

static gpu_device_info g_primary;
static gpu_firmware_set g_firmware;
static gpu_iommu_domain g_iommu_domain;
static gpu_iommu_topology g_iommu_topology;
static gpu_iommu_attach_plan g_iommu_attach_plan;
static gpu_scheduler g_scheduler;
static gpu_command_ring g_submission_ring;
static gpu_command_ring g_runtime_ring;
static gpu_buffer g_cp_shadow;
static gpu_buffer g_cp_pwrup;
static gpu_buffer g_cp_completion;
static gpu_buffer g_mesart_shader_pool;
static gpu_buffer g_mesart_uniform_pool;
static gpu_scanout_target g_scanout_target;
/* Retained until GPU teardown: never release backing while DMA can refer to it. */
static gpu_render_target g_triangle_target;
static uint64_t g_triangle_target_iova;
static gpu_render_target g_colour_write_probe;
static uint64_t g_colour_write_probe_iova;
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
static uint8_t g_runtime_backend_ready;
/* A Gen7 GPU_SET lease is established only after the boot submission has
 * proved that CP, the SMMU, and GX are healthy.  Keeping that one lease while
 * the experimental runtime is usable avoids a firmware power transition
 * between CP setup and the first trusted draw. */
static uint8_t g_runtime_gx_lease_held;
/* Set only after SQE has been bound to g_runtime_ring.  Runtime clients may
 * then append at CP_RB_RPTR without restarting SQE or losing its wrap phase. */
static uint8_t g_runtime_ring_bound;
static uint64_t g_runtime_3d_sequence;
/* The trusted 3D encoder owns the one live runtime ring until RB has written
 * its completion markers.  A later compositor will retain its pipeline and
 * render target until this fence completes; neither EL0 nor KGFX can touch
 * this state. */
typedef struct gpu_core_mesart_3d_inflight
{
    uint64_t fence;
    uint64_t started_counter;
    uint32_t expected_rptr;
    uint32_t poll_count;
    uint32_t completion_markers[9];
    uint32_t source_first_expected;
    uint32_t source_last_expected;
    uint32_t descriptor_expected[5];
    /* A deliberately seeded 3x3 patch at the centre of the first trusted
     * triangle.  RB_DONE proves the backend retired, but this witness tells
     * us whether that backend actually wrote the imported display surface. */
    uint32_t scanout_probe_marker;
    uint32_t scanout_probe_after;
    uint8_t scanout_probe_changed;
    uint8_t compositor_present;
    uint8_t compositor_triangle_half;
    uint8_t active;
} gpu_core_mesart_3d_inflight;

static gpu_core_mesart_3d_inflight g_mesart_3d_inflight;
static uint8_t g_mesart_3d_frame_ready;

#define GPU_SMMUV2_FSR_FAULT_MASK 0xC00001FEu

#define GPU_CORE_STAGING_IOVA_BASE 0x0000000010000000ull
#define GPU_CORE_SUBMISSION_IOVA   0x0000000020000000ull
#define GPU_CORE_SUBMISSION_BYTES  0x00008000u
#define GPU_CORE_RUNTIME_RING_IOVA 0x0000000020008000ull
#define GPU_CORE_RUNTIME_RING_BYTES 0x00008000u
#define GPU_CORE_CP_SHADOW_IOVA     0x0000000020010000ull
#define GPU_CORE_CP_PWRUP_IOVA      0x0000000020011000ull
#define GPU_CORE_CP_COMPLETION_IOVA 0x0000000020012000ull
#define GPU_CORE_SCANOUT_IOVA      0x0000000030000000ull
#define GPU_CORE_MESART_GPU_VA_BASE 0x0000000040000000ull
#define GPU_CORE_MESART_GPU_VA_END  0x0000000060000000ull
#define GPU_CORE_MESART_SHADER_POOL_IOVA 0x0000000042000000ull
/* One slot holds a complete authenticated MIR3 source at offset zero and a
 * separately aligned executable copy at offset 128.  The extra page makes
 * the maximum accepted four-megabyte binary fit in both placements. */
#define GPU_CORE_MESART_SHADER_SLOT_BYTES \
    (MESART_IR3_BLOB_MAX_CODE_BYTES + GPU_IOMMU_PAGE_SIZE)
#define GPU_CORE_MESART_SHADER_SLOT_COUNT 4u
#define GPU_CORE_MESART_SHADER_POOL_BYTES \
    ((uint64_t)GPU_CORE_MESART_SHADER_SLOT_COUNT * \
     GPU_CORE_MESART_SHADER_SLOT_BYTES)
#define GPU_CORE_MESART_UNIFORM_POOL_IOVA 0x0000000043100000ull
#define GPU_CORE_MESART_RESOURCE_STRIDE     0x400u
#define GPU_CORE_MESART_RESOURCE_SLOTS      GPU_RENDER_MAX_BATCH_DRAWS
#define GPU_CORE_MESART_UNIFORM_POOL_BYTES \
    ((uint64_t)GPU_CORE_MESART_RESOURCE_STRIDE * GPU_CORE_MESART_RESOURCE_SLOTS)
#define GPU_CORE_MESART_VERTEX_UBO_OFFSET   0u
#define GPU_CORE_MESART_FRAGMENT_UBO_OFFSET 64u
#define GPU_CORE_MESART_DEFAULT_UBO_BYTES   64u
/* The initial vertex proof is deliberately kernel-owned data rather than a
 * Mesart EL0 resource.  It shares the private uniform pool only because both
 * use the same already-mapped unprivileged GPU read transaction class. */
#define GPU_CORE_MESART_VERTEX_BUFFER_OFFSET 0x100u
#define GPU_CORE_MESART_POSITION_BUFFER_BYTES 24u
#define GPU_CORE_MESART_POSITION_BUFFER_STRIDE 8u
#define GPU_CORE_MESART_TEXTURED_BUFFER_BYTES 48u
#define GPU_CORE_MESART_TEXTURED_QUAD_BUFFER_BYTES 96u
#define GPU_CORE_MESART_TEXTURED_BUFFER_STRIDE 16u
#define GPU_CORE_MESART_SAMPLER_DESC_OFFSET   0x200u
#define GPU_CORE_MESART_TEXTURE_DESC_OFFSET   0x240u
#define GPU_CORE_MESART_TEXTURE_PIXELS_OFFSET 0x400u
#define GPU_CORE_MESART_TEXTURE_WIDTH         16u
#define GPU_CORE_MESART_TEXTURE_HEIGHT        16u
#define GPU_CORE_MESART_TEXTURE_PITCH         64u
#define GPU_CORE_MESART_TEXTURE_BYTES         1024u
/* 0x60000000 belongs to the GMU's fixed dummy/HFI/log aperture.  Mapping
 * render surface slot zero there let the later GMU dummy page silently
 * replace the desktop texture's first leaf with a privileged mapping. */
#define GPU_CORE_RENDER_SURFACE_VA_BASE 0x0000000070000000ull
#define GPU_CORE_RENDER_SURFACE_SLOT_BYTES GPU_RENDER_MAX_SURFACE_BYTES
#define GPU_CORE_RENDER_SURFACE_VA_END \
    (GPU_CORE_RENDER_SURFACE_VA_BASE + \
     (uint64_t)GPU_RENDER_MAX_SURFACES * GPU_CORE_RENDER_SURFACE_SLOT_BYTES)
_Static_assert(GPU_CORE_RENDER_SURFACE_VA_BASE >=
               GPU_GMU_DEBUG_IOVA + GPU_GMU_DEBUG_BYTES,
               "render surfaces must not overlap GMU fixed IOVAs");

/* A6xx/A7xx texture-descriptor fields from Mesa's vendored
 * a6xx_descriptors.xml.  Keep the packers next to the one kernel-owned
 * descriptor producer: open-coded shifts made the small 16x16 proof hide
 * fields which become significant for a full desktop-sized layer. */
#define A7XX_TEX_SWIZ_X(value)       (((uint32_t)(value) & 0x7u) << 4)
#define A7XX_TEX_SWIZ_Y(value)       (((uint32_t)(value) & 0x7u) << 7)
#define A7XX_TEX_SWIZ_Z(value)       (((uint32_t)(value) & 0x7u) << 10)
#define A7XX_TEX_SWIZ_W(value)       (((uint32_t)(value) & 0x7u) << 13)
#define A7XX_TEX_FORMAT(value)       (((uint32_t)(value) & 0xffu) << 22)
#define A7XX_TEX_SWAP(value)         (((uint32_t)(value) & 0x3u) << 30)
#define A7XX_TEX_WIDTH(value)        ((uint32_t)(value) & 0x7fffu)
#define A7XX_TEX_HEIGHT(value)       (((uint32_t)(value) & 0x7fffu) << 15)
#define A7XX_TEX_PITCH_ALIGN(value)  ((uint32_t)(value) & 0xfu)
#define A7XX_TEX_PITCH(value)        (((uint32_t)(value) & 0x3fffffu) << 7)
#define A7XX_TEX_TYPE(value)         (((uint32_t)(value) & 0x7u) << 29)
#define A7XX_TEX_ARRAY_PITCH(bytes)  \
    ((uint32_t)(((uint64_t)(bytes) >> 12) & 0x7fffffu))
#define A7XX_TEX_DEPTH(value)        (((uint32_t)(value) & 0x1fffu) << 17)

#define A7XX_TEX_X                    0u
#define A7XX_TEX_Y                    1u
#define A7XX_TEX_Z                    2u
#define A7XX_TEX_ONE                  5u
#define A7XX_TEX_FMT_8_8_8_8_UNORM  0x30u
#define A7XX_TEX_SWAP_WXYZ            1u
#define A7XX_TEX_TYPE_2D              1u

/* Gen7's hardware shadow layout is rptr, fence, then BV rptr.  The fence
 * slot is CPU bookkeeping, not a second hardware rptr destination. */
#define GPU_CORE_CP_BR_RPTR_OFFSET  0u
#define GPU_CORE_CP_BV_RPTR_OFFSET  8u
#define GPU_CORE_CP_COMPLETION_MAGIC 0x43504F4Bu /* "CPOK" */
#define GPU_CORE_RUNTIME_COMPLETION_MAGIC 0x52554E00u /* "RUN\0" */
#define GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET        40u
#define GPU_CORE_MESART_STATE_SNAPSHOT_BYTES        104u
#define GPU_CORE_MESART_RT_STATE_SNAPSHOT_OFFSET      144u
#define GPU_CORE_MESART_RT_STATE_SNAPSHOT_BYTES        80u
#define GPU_CORE_COMPOSITOR_SOURCE_FIRST_OFFSET        224u
#define GPU_CORE_COMPOSITOR_SOURCE_LAST_OFFSET         228u
#define GPU_CORE_COMPOSITOR_DESCRIPTOR_OFFSET          232u
#define GPU_CORE_MESART_STAGE_COUNTER_START_OFFSET    256u
#define GPU_CORE_MESART_STAGE_COUNTER_END_OFFSET      304u
#define GPU_CORE_MESART_STAGE_COUNTER_BYTES            48u
#define GPU_CORE_MESART_RT_GPU_READBACK_OFFSET         352u
#define GPU_CORE_MESART_CCU_SNAPSHOT_OFFSET            356u
#define GPU_CORE_MESART_CCU_START_OFFSET               368u
#define GPU_CORE_MESART_CCU_END_OFFSET                 408u
#define GPU_CORE_MESART_RT_POST_DRAW_OFFSET            448u
#define GPU_CORE_MESART_CP_WRITE_PROBE_OFFSET          528u
#define GPU_CORE_MESART_BLIT_READBACK_OFFSET           532u
#define GPU_CORE_MESART_UFC_START_OFFSET               536u
#define GPU_CORE_MESART_UFC_END_OFFSET                 568u
#define GPU_CORE_MESART_UFC_BLIT_OFFSET                600u
#define GPU_CORE_MESART_RBBM_START_OFFSET              632u
#define GPU_CORE_MESART_RBBM_DRAW_OFFSET               636u
#define GPU_CORE_MESART_RBBM_BLIT_OFFSET               640u
#define GPU_CORE_MESART_SECURE_BEGIN_OFFSET            644u
#define GPU_CORE_MESART_SECURE_END_OFFSET              648u
#define GPU_CORE_MESART_DIAGNOSTIC_BYTES \
    (GPU_CORE_MESART_SECURE_END_OFFSET + 4u)
#define GPU_CORE_MESART_3D_TIMEOUT_SECONDS 2u
#define GPU_CORE_COMPOSITOR_TIMEOUT_SECONDS 2u

/* dihos_time_ticks() counts main-loop iterations, not elapsed wall time.
 * In an asynchronous GPU frame that loop can run much faster than 60 Hz,
 * causing a live draw to be power-cycled before it finishes. */
static uint64_t gpu_core_monotonic_counter(void)
{
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    uint64_t counter;
    __asm__ __volatile__("isb; mrs %0, cntvct_el0" : "=r"(counter));
    return counter;
#else
    return dihos_time_ticks();
#endif
}

static uint64_t gpu_core_monotonic_frequency(void)
{
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    uint64_t frequency;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(frequency));
    return frequency;
#else
    return DIHOS_TIME_TICKS_PER_SECOND;
#endif
}
#define GPU_CORE_GOP_BGRX_8888       1u
#define GPU_CORE_MESART_SCANOUT_PROBE_MARKER 0xff12c4a5u

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
    gpu_ring_release(&g_runtime_ring);
    gpu_buffer_release(&g_cp_shadow);
    gpu_buffer_release(&g_cp_pwrup);
    gpu_buffer_release(&g_cp_completion);
    gpu_buffer_release(&g_mesart_shader_pool);
    gpu_buffer_release(&g_mesart_uniform_pool);
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
        gpu_ring_init(&g_runtime_ring, GPU_CORE_RUNTIME_RING_BYTES) != 0 ||
        gpu_iommu_map_buffer(&g_iommu_domain, &g_runtime_ring.buffer,
                             GPU_CORE_RUNTIME_RING_IOVA) != 0 ||
        gpu_buffer_alloc(&g_cp_shadow, 0x1000u,
                         GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0 ||
        gpu_iommu_map_buffer(&g_iommu_domain, &g_cp_shadow,
                             GPU_CORE_CP_SHADOW_IOVA) != 0 ||
        gpu_buffer_alloc(&g_cp_pwrup, 0x1000u,
                         GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0 ||
        gpu_iommu_map_buffer(&g_iommu_domain, &g_cp_pwrup,
                             GPU_CORE_CP_PWRUP_IOVA) != 0 ||
        gpu_buffer_alloc(&g_cp_completion, 0x1000u,
                         GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0 ||
        gpu_iommu_map_buffer(&g_iommu_domain, &g_cp_completion,
                             GPU_CORE_CP_COMPLETION_IOVA) != 0 ||
        gpu_buffer_alloc(&g_mesart_shader_pool,
                         GPU_CORE_MESART_SHADER_POOL_BYTES,
                         GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0 ||
        /* SQ instruction fetch uses an unprivileged DMA attribute on this
         * Gen7 path.  This is an IOMMU transaction permission only: unlike
         * Mesart resources, the pool is never inserted into the EL0 VM. */
        gpu_iommu_map_buffer_with_attributes(&g_iommu_domain,
                                             &g_mesart_shader_pool,
                                             GPU_CORE_MESART_SHADER_POOL_IOVA,
                                             0u) != 0 ||
        gpu_buffer_alloc(&g_mesart_uniform_pool,
                         GPU_CORE_MESART_UNIFORM_POOL_BYTES,
                         GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0 ||
        /* Shader UBO reads use the same unprivileged GPU transaction class
         * as instruction fetch.  The memory is nevertheless kernel-only:
         * this mapping is never present in the renderer's EL0 page table. */
        gpu_iommu_map_buffer_with_attributes(&g_iommu_domain,
                                             &g_mesart_uniform_pool,
                                             GPU_CORE_MESART_UNIFORM_POOL_IOVA,
                                             0u) != 0)
    {
        gpu_ring_release(&g_submission_ring);
        gpu_ring_release(&g_runtime_ring);
        gpu_buffer_release(&g_cp_shadow);
        gpu_buffer_release(&g_cp_pwrup);
        gpu_buffer_release(&g_cp_completion);
        gpu_buffer_release(&g_mesart_shader_pool);
        gpu_buffer_release(&g_mesart_uniform_pool);
        gpu_iommu_domain_release(&g_iommu_domain);
        return -3;
    }
    gpu_buffer_prepare_for_device(&g_cp_shadow);
    gpu_buffer_prepare_for_device(&g_cp_pwrup);
    gpu_buffer_prepare_for_device(&g_cp_completion);
    gpu_buffer_prepare_for_device(&g_mesart_shader_pool);
    gpu_buffer_prepare_for_device(&g_mesart_uniform_pool);
    if (g_scanout_target.buffer.cpu &&
        /* Gen7 RB colour stores use the same unprivileged transaction class
         * as SQ instruction fetch and UBO reads.  CP can still address this
         * mapping from EL1, whereas a privileged-only scanout mapping lets
         * CP's boot triangle work but drops the 3D render-target writes. */
        gpu_iommu_map_buffer_with_attributes(
            &g_iommu_domain, &g_scanout_target.buffer,
            GPU_CORE_SCANOUT_IOVA, 0u) != 0)
    {
        gpu_ring_release(&g_submission_ring);
        gpu_ring_release(&g_runtime_ring);
        gpu_buffer_release(&g_cp_shadow);
        gpu_buffer_release(&g_cp_pwrup);
        gpu_buffer_release(&g_cp_completion);
        gpu_buffer_release(&g_mesart_shader_pool);
        gpu_buffer_release(&g_mesart_uniform_pool);
        gpu_iommu_domain_release(&g_iommu_domain);
        return -4;
    }
    /* Map every surface that existed before GPU discovery into the initial
     * domain.  X1E's GPU-local SMMU is not a reliable place to grow page
     * tables after GMU ownership changes, so the desktop surface must not be
     * first introduced by the live compositor submission path. */
    for (uint32_t slot = 0u; slot < GPU_RENDER_MAX_SURFACES; ++slot)
    {
        gpu_buffer *surface = 0;
        int surface_rc = gpu_render_backend_surface_slot(slot, &surface);
        uint64_t surface_iova = GPU_CORE_RENDER_SURFACE_VA_BASE +
            (uint64_t)slot * GPU_CORE_RENDER_SURFACE_SLOT_BYTES;

        if (surface_rc > 0)
            continue;
        if (surface_rc < 0 || !surface || surface->iova ||
            surface->size_bytes > GPU_CORE_RENDER_SURFACE_SLOT_BYTES ||
            gpu_iommu_map_buffer_with_attributes(&g_iommu_domain, surface,
                                                  surface_iova, 0u) != 0)
        {
            gpu_ring_release(&g_submission_ring);
            gpu_ring_release(&g_runtime_ring);
            gpu_buffer_release(&g_cp_shadow);
            gpu_buffer_release(&g_cp_pwrup);
            gpu_buffer_release(&g_cp_completion);
            gpu_buffer_release(&g_mesart_shader_pool);
            gpu_buffer_release(&g_mesart_uniform_pool);
            gpu_iommu_domain_release(&g_iommu_domain);
            return -5;
        }
    }
    if (gpu_gmu_memory_map(&g_iommu_domain, &g_gmu_memory) != 0)
    {
        gpu_ring_release(&g_submission_ring);
        gpu_ring_release(&g_runtime_ring);
        gpu_buffer_release(&g_cp_shadow);
        gpu_buffer_release(&g_cp_pwrup);
        gpu_buffer_release(&g_cp_completion);
        gpu_buffer_release(&g_mesart_shader_pool);
        gpu_buffer_release(&g_mesart_uniform_pool);
        gpu_iommu_domain_release(&g_iommu_domain);
        return -6;
    }
    gpu_iommu_domain_prepare_for_device(&g_iommu_domain);
    return 0;
}

int gpu_core_init(const boot_info *boot)
{
    adreno_x1_85_profile profile;
    int rc;

    g_primary = (gpu_device_info){0};
    gpu_scheduler_init(&g_scheduler);
    g_triangle_target = (gpu_render_target){0};
    g_triangle_target_iova = 0u;
    g_colour_write_probe = (gpu_render_target){0};
    g_colour_write_probe_iova = 0u;
    g_iommu_topology = (gpu_iommu_topology){0};
    g_iommu_attach_plan = (gpu_iommu_attach_plan){0};
    g_gpu_smmu_caps = (gpu_smmuv2_caps){0};
    g_gpu_smmu_context_bank = 0u;
    g_gpu_smmu_bound_stream_count = 0u;
    g_gmu_smmu_context_bank = 0u;
    g_gmu_smmu_bound_stream_count = 0u;
    g_gmu_image = (gpu_gmu_image){0};
    g_gmu_reset_signature = 0u;
    g_runtime_backend_ready = 0u;
    g_runtime_gx_lease_held = 0u;
    g_runtime_ring_bound = 0u;
    g_runtime_3d_sequence = 0u;
    g_mesart_3d_inflight = (gpu_core_mesart_3d_inflight){0};
    g_mesart_3d_frame_ready = 0u;
    gpu_gmu_memory_release(&g_gmu_memory);
    rpmh_cmd_db_mapping_release(&g_rpmh_cmd_db);
    gpu_buffer_release(&g_scanout_target.buffer);
    g_scanout_target = (gpu_scanout_target){0};
    gpu_iommu_domain_release(&g_iommu_domain);
    gpu_ring_release(&g_submission_ring);
    gpu_ring_release(&g_runtime_ring);
    gpu_buffer_release(&g_cp_shadow);
    gpu_buffer_release(&g_cp_pwrup);
    gpu_buffer_release(&g_cp_completion);
    gpu_buffer_release(&g_mesart_shader_pool);
    gpu_buffer_release(&g_mesart_uniform_pool);
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
                                                            {
                                                                *(uint32_t *)g_cp_completion.cpu = 0u;
                                                                gpu_buffer_prepare_for_device(
                                                                    &g_cp_completion);
                                                                cp_rc = adreno_x1_85_emit_cp_memory_probe(
                                                                    &g_submission_ring,
                                                                    g_cp_completion.iova,
                                                                    GPU_CORE_CP_COMPLETION_MAGIC);
                                                            }
                                                            if (cp_rc == 0 &&
                                                                g_scanout_target.buffer.iova &&
                                                                g_scanout_target.buffer.size_bytes <=
                                                                    0xffffffffull &&
                                                                g_scanout_target.pixel_format ==
                                                                    GPU_CORE_GOP_BGRX_8888)
                                                            {
                                                                terminal_print(
                                                                    "[K:GPU] emitting visible CP scanout triangle");
                                                                /* Flush any CPU-owned scanout cache lines before
                                                                 * CP overwrites the centre test region.  The later
                                                                 * wait packet makes those CP writes visible before
                                                                 * the GX lease is released. */
                                                                asm_dma_clean_range(
                                                                    g_scanout_target.buffer.cpu,
                                                                    g_scanout_target.buffer.size_bytes);
                                                                cp_rc =
                                                                    adreno_x1_85_emit_cp_scanout_triangle(
                                                                        &g_submission_ring,
                                                                        g_scanout_target.buffer.iova,
                                                                        (uint32_t)g_scanout_target.buffer.size_bytes,
                                                                        g_scanout_target.width,
                                                                        g_scanout_target.height,
                                                                        g_scanout_target.pitch);
                                                            }
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
                                                                terminal_print("[K:GPU] submitting Gen7 CP init + memory probe");
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
                                                            if (cp_rc == 0)
                                                            {
                                                                uint32_t completion;

                                                                asm_dma_invalidate_range(
                                                                    g_cp_completion.cpu,
                                                                    sizeof(completion));
                                                                completion = *(uint32_t *)g_cp_completion.cpu;
                                                                if (completion !=
                                                                    GPU_CORE_CP_COMPLETION_MAGIC)
                                                                    cp_rc = -17;
                                                                else
                                                                {
                                                                    terminal_print(
                                                                        "[K:GPU] CP memory probe completed value=");
                                                                    terminal_print_inline_hex64(completion);
                                                                    if (g_scanout_target.buffer.iova &&
                                                                        g_scanout_target.buffer.size_bytes <=
                                                                            0xffffffffull &&
                                                                        g_scanout_target.pixel_format ==
                                                                            GPU_CORE_GOP_BGRX_8888)
                                                                    {
                                                                        asm_dma_invalidate_range(
                                                                            g_scanout_target.buffer.cpu,
                                                                            g_scanout_target.buffer.size_bytes);
                                                                        terminal_print(
                                                                            "[K:GPU] CP scanout triangle submitted");
                                                                    }
                                                                }
                                                            }
                                                            if (cp_rc == 0 && !cp.hw_fault &&
                                                                !cp.protect_status)
                                                            {
                                                                /* The automatic boot probe has consumed a complete
                                                                 * CP init ring without an SMMU or CP fault.  Transfer
                                                                 * its still-held GX lease to the runtime.  X1E firmware
                                                                 * can fault when this exact lease is immediately
                                                                 * released and reacquired, even though the live CP is
                                                                 * healthy. */
                                                                g_runtime_backend_ready = 1u;
                                                                g_runtime_gx_lease_held = 1u;
                                                                terminal_print("[K:GPU] CP startup ring consumed rptr=");
                                                                terminal_print_inline_hex64(cp.rb_rptr);
                                                                terminal_print(" wptr=");
                                                                terminal_print_inline_hex64(cp.rb_wptr);
                                                                terminal_print(" sqe=");
                                                                terminal_print_inline_hex64(cp.sqe_control);
                                                            }
                                                            else
                                                            {
                                                                terminal_warn("[K:GPU] CP startup ring did not complete cleanly");
                                                                terminal_print("[K:GPU] CP startup rc=");
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
                                                    if (!g_runtime_gx_lease_held &&
                                                        gpu_gmu_gen7_release_gpu(
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

gpu_scheduler *gpu_core_scheduler(void)
{
    return &g_scheduler;
}

int gpu_core_map_mesart_buffer(gpu_buffer *buffer, uint64_t gpu_va)
{
    uint64_t bytes;

    if (!buffer || !buffer->cpu || !buffer->phys || !buffer->size_bytes ||
        buffer->iova || (gpu_va & (GPU_IOMMU_PAGE_SIZE - 1u)) ||
        g_mesart_3d_inflight.active || g_scheduler.job_count ||
        g_gpu_smmu_bound_stream_count != ADRENO_X1_85_DRIVER_SID_COUNT ||
        !(g_primary.caps.flags & GPU_CAP_DMA_ISOLATED) ||
        !g_iommu_domain.root_phys)
        return -1;
    bytes = (buffer->size_bytes + GPU_IOMMU_PAGE_SIZE - 1u) &
            ~(GPU_IOMMU_PAGE_SIZE - 1u);
    if (!bytes || bytes < buffer->size_bytes ||
        bytes > GPU_CORE_MESART_GPU_VA_END - GPU_CORE_MESART_GPU_VA_BASE ||
        gpu_va < GPU_CORE_MESART_GPU_VA_BASE ||
        gpu_va - GPU_CORE_MESART_GPU_VA_BASE >
            (GPU_CORE_MESART_GPU_VA_END - GPU_CORE_MESART_GPU_VA_BASE) - bytes)
        return -2;
    if (gpu_iommu_map_buffer(&g_iommu_domain, buffer, gpu_va) != 0)
        return -3;
    gpu_iommu_domain_prepare_for_device(&g_iommu_domain);
    /* Each Mesart slot has a fixed, previously unused IOVA and this helper
     * rejects all live GPU work.  Thus no translation for this range can be
     * resident in CB0 when we publish its freshly cleaned page-table leaves.
     * The GPU-local X1E SMMU's global TLBI register can itself external-abort
     * after GMU ownership changes, so do not issue an unnecessary invalidate
     * here.  Replacing/reusing a mapped IOVA remains forbidden; such a path
     * must grow an explicit, hardware-confirmed TLB protocol first. */
    return 0;
}

int gpu_core_mesart_shader_slot(uint32_t slot, gpu_buffer **out_storage,
                                uint64_t *out_offset,
                                uint64_t *out_gpu_va,
                                uint64_t *out_bytes)
{
    uint64_t offset;

    if (!out_storage || !out_offset || !out_gpu_va || !out_bytes ||
        slot >= GPU_CORE_MESART_SHADER_SLOT_COUNT ||
        !g_mesart_shader_pool.cpu || !g_mesart_shader_pool.phys ||
        g_mesart_shader_pool.iova != GPU_CORE_MESART_SHADER_POOL_IOVA ||
        g_mesart_shader_pool.size_bytes != GPU_CORE_MESART_SHADER_POOL_BYTES ||
        g_mesart_3d_inflight.active || g_scheduler.job_count ||
        g_gpu_smmu_bound_stream_count != ADRENO_X1_85_DRIVER_SID_COUNT ||
        !(g_primary.caps.flags & GPU_CAP_DMA_ISOLATED))
        return -1;
    offset = (uint64_t)slot * GPU_CORE_MESART_SHADER_SLOT_BYTES;
    if (offset > g_mesart_shader_pool.size_bytes ||
        GPU_CORE_MESART_SHADER_SLOT_BYTES >
            g_mesart_shader_pool.size_bytes - offset)
        return -2;
    *out_storage = &g_mesart_shader_pool;
    *out_offset = offset;
    *out_gpu_va = GPU_CORE_MESART_SHADER_POOL_IOVA + offset;
    *out_bytes = GPU_CORE_MESART_SHADER_SLOT_BYTES;
    return 0;
}

static int gpu_core_mesart_configure_vertex_buffer(
    const mesart_renderer_graphics_pipeline *pipeline,
    adreno_x1_85_3d_draw *draw, uint32_t write_values,
    uint32_t fullscreen, const gpu_render_rect *source_rect,
    uint32_t source_width, uint32_t source_height, uint32_t resource_slot)
{
    const mesart_pipeline_header *reflection;
    uint32_t *words;
    uint64_t resource_offset = (uint64_t)resource_slot *
                               GPU_CORE_MESART_RESOURCE_STRIDE;

    if (!pipeline || !draw || !pipeline->pipeline_reflection_ready ||
        !g_mesart_uniform_pool.cpu ||
        g_mesart_uniform_pool.iova != GPU_CORE_MESART_UNIFORM_POOL_IOVA ||
        resource_slot >= GPU_CORE_MESART_RESOURCE_SLOTS ||
        resource_offset + GPU_CORE_MESART_VERTEX_BUFFER_OFFSET +
            GPU_CORE_MESART_TEXTURED_QUAD_BUFFER_BYTES >
                g_mesart_uniform_pool.size_bytes)
        return -1;
    reflection = &pipeline->reflection;
    if (reflection->vertex_attribute_count != 1u ||
        /* IR3 encodes GLSL location zero as VERT_ATTRIB_GENERIC0 (slot 15). */
        reflection->vertex_attribute0_slot != 15u ||
        (reflection->vertex_attribute0_compmask != 0x3u &&
         reflection->vertex_attribute0_compmask != 0xfu))
        return -2;
    draw->vertex_buffer_gpu_va = GPU_CORE_MESART_UNIFORM_POOL_IOVA + resource_offset +
                                 GPU_CORE_MESART_VERTEX_BUFFER_OFFSET;
    draw->vertex_buffer_stride = reflection->vertex_attribute0_compmask == 0xfu ?
        GPU_CORE_MESART_TEXTURED_BUFFER_STRIDE :
        GPU_CORE_MESART_POSITION_BUFFER_STRIDE;
    if (!draw->vertex_count)
        draw->vertex_count = 3u;
    draw->vertex_buffer_bytes = draw->vertex_count * draw->vertex_buffer_stride;
    if (!write_values)
        return 0;

    /* A centred, ordinary triangle. The tint profile adds a kernel-owned
     * translation through default UBO 0, independently proving vertex fetch
     * and live uniform consumption without accepting host GPU addresses. */
    words = (uint32_t *)((uint8_t *)g_mesart_uniform_pool.cpu + resource_offset +
                         GPU_CORE_MESART_VERTEX_BUFFER_OFFSET);
    /* GOP memory is top-down, so invert Y in the test geometry while
     * retaining counter-clockwise clip-space winding.  A negative hardware
     * viewport scale made this X1-85 TSE path reject the primitive. */
    if (reflection->vertex_attribute0_compmask == 0xfu)
    {
        const uint32_t textured_vertices[12] = {
            0x00000000u, 0xbf266666u, 0x3f000000u, 0x00000000u,
            0x3f266666u, 0x3f266666u, 0x3f800000u, 0x3f800000u,
            0xbf266666u, 0x3f266666u, 0x00000000u, 0x3f800000u,
        };
        if (fullscreen)
        {
            /* Two ordinary in-clip triangles avoid the oversized-triangle
             * clipping path.  The latter was the last state difference from
             * the working textured proof and left SP/RB permanently busy on
             * X1-85 even with a small, tightly packed source and target. */
            /* Reuse the proven +/-0.65 clip envelope. The compositor draw
             * expands this through its kernel-owned viewport transform, so
             * it covers the tile without touching the fragile clip planes. */
            const uint32_t fullscreen_vertices[24] = {
                0xbf266666u, 0xbf266666u, 0x00000000u, 0x00000000u,
                0x3f266666u, 0xbf266666u, 0x3f800000u, 0x00000000u,
                0xbf266666u, 0x3f266666u, 0x00000000u, 0x3f800000u,
                0x3f266666u, 0xbf266666u, 0x3f800000u, 0x00000000u,
                0x3f266666u, 0x3f266666u, 0x3f800000u, 0x3f800000u,
                0xbf266666u, 0x3f266666u, 0x00000000u, 0x3f800000u,
            };
            if (fullscreen == 3u)
            {
                /* A7xx RECTLIST's two diagonal corners. VPC replacement
                 * creates the other two corners and their UVs. */
                const uint32_t rectangle_vertices[8] = {
                    0xbf266666u, 0xbf266666u, 0x00000000u, 0x00000000u,
                    0x3f266666u, 0x3f266666u, 0x3f800000u, 0x3f800000u,
                };
                for (uint32_t i = 0u; i < 8u; ++i)
                    words[i] = rectangle_vertices[i];
            }
            else if (fullscreen == 4u)
            {
                /* Complete quad in one ordinary TRI_LIST submission. This
                 * keeps VPC on its proven triangle path while avoiding a
                 * second render/cache submission for the same tile. */
                if (source_rect && source_rect->width && source_rect->height)
                {
                    union { float value; uint32_t bits; } u0, v0, u1, v1;
                    if (!source_width)
                        source_width = draw->target->width;
                    if (!source_height)
                        source_height = draw->target->height;
                    u0.value = (float)source_rect->x / (float)source_width;
                    v0.value = (float)source_rect->y / (float)source_height;
                    u1.value = (float)(source_rect->x + source_rect->width) /
                               (float)source_width;
                    v1.value = (float)(source_rect->y + source_rect->height) /
                               (float)source_height;
                    const uint32_t damaged_vertices[24] = {
                        0xbf266666u, 0xbf266666u, u0.bits, v0.bits,
                        0x3f266666u, 0xbf266666u, u1.bits, v0.bits,
                        0xbf266666u, 0x3f266666u, u0.bits, v1.bits,
                        0x3f266666u, 0xbf266666u, u1.bits, v0.bits,
                        0x3f266666u, 0x3f266666u, u1.bits, v1.bits,
                        0xbf266666u, 0x3f266666u, u0.bits, v1.bits,
                    };
                    for (uint32_t i = 0u; i < 24u; ++i)
                        words[i] = damaged_vertices[i];
                }
                else
                    for (uint32_t i = 0u; i < 24u; ++i)
                        words[i] = fullscreen_vertices[i];
            }
            else
            {
                uint32_t first = fullscreen == 2u ? 12u : 0u;
                for (uint32_t i = 0u; i < 12u; ++i)
                    words[i] = fullscreen_vertices[first + i];
            }
        }
        else
            for (uint32_t i = 0u; i < 12u; ++i)
                words[i] = textured_vertices[i];
    }
    else
    {
        words[0] = 0x00000000u; /*  0.00 */
        words[1] = 0xbf266666u; /* -0.65: visible apex */
        words[2] = 0x3f266666u; /*  0.65 */
        words[3] = 0x3f266666u; /*  0.65 */
        words[4] = 0xbf266666u; /* -0.65 */
        words[5] = 0x3f266666u; /*  0.65 */
    }
    asm_dma_clean_range(words, draw->vertex_buffer_bytes);
    return 0;
}

/* The first parameterized hardware profile is deliberately fixed-data: it
 * proves Mesa's default-UBO ABI without giving an EL0 process a descriptor or
 * a GPU address.  The eventual driver-neutral resource API will replace
 * these literals with validated kernel-side parameter blocks. */
static int gpu_core_mesart_configure_default_uniforms(
    const mesart_renderer_graphics_pipeline *pipeline,
    adreno_x1_85_3d_draw *draw, uint32_t write_values,
    uint32_t identity_transform, uint32_t resource_slot)
{
    const mesart_pipeline_header *reflection;
    uint32_t *words;
    uint64_t resource_offset = (uint64_t)resource_slot *
                               GPU_CORE_MESART_RESOURCE_STRIDE;

    if (!pipeline || !draw)
        return -1;
    reflection = &pipeline->reflection;
    if (reflection->vertex_app_ubo_count > 1u ||
        reflection->fragment_app_ubo_count > 1u)
        return -2;
    if (!reflection->vertex_app_ubo_count &&
        !reflection->fragment_app_ubo_count)
        return 0;
    if (!g_mesart_uniform_pool.cpu ||
        g_mesart_uniform_pool.iova != GPU_CORE_MESART_UNIFORM_POOL_IOVA ||
        g_mesart_uniform_pool.size_bytes != GPU_CORE_MESART_UNIFORM_POOL_BYTES ||
        resource_slot >= GPU_CORE_MESART_RESOURCE_SLOTS ||
        resource_offset + GPU_CORE_MESART_FRAGMENT_UBO_OFFSET +
            GPU_CORE_MESART_DEFAULT_UBO_BYTES >
                g_mesart_uniform_pool.size_bytes)
        return -3;
    if (reflection->vertex_app_ubo_count)
    {
        draw->vertex_default_ubo_gpu_va =
            GPU_CORE_MESART_UNIFORM_POOL_IOVA + resource_offset +
            GPU_CORE_MESART_VERTEX_UBO_OFFSET;
        draw->vertex_default_ubo_bytes = GPU_CORE_MESART_DEFAULT_UBO_BYTES;
        draw->vertex_default_ubo_values =
            (const uint32_t *)((const uint8_t *)g_mesart_uniform_pool.cpu + resource_offset +
                               GPU_CORE_MESART_VERTEX_UBO_OFFSET);
    }
    if (reflection->fragment_app_ubo_count)
    {
        draw->fragment_default_ubo_gpu_va =
            GPU_CORE_MESART_UNIFORM_POOL_IOVA + resource_offset +
            GPU_CORE_MESART_FRAGMENT_UBO_OFFSET;
        draw->fragment_default_ubo_bytes = GPU_CORE_MESART_DEFAULT_UBO_BYTES;
        draw->fragment_default_ubo_values =
            (const uint32_t *)((const uint8_t *)g_mesart_uniform_pool.cpu + resource_offset +
                               GPU_CORE_MESART_FRAGMENT_UBO_OFFSET);
    }
    if (!write_values)
        return 0;

    words = (uint32_t *)((uint8_t *)g_mesart_uniform_pool.cpu + resource_offset);
    for (uint32_t i = 0u; i < GPU_CORE_MESART_DEFAULT_UBO_BYTES / 4u; ++i)
        words[i] = 0u;
    uint32_t *fragment_words =
        (uint32_t *)((uint8_t *)g_mesart_uniform_pool.cpu + resource_offset +
                     GPU_CORE_MESART_FRAGMENT_UBO_OFFSET);
    for (uint32_t i = 0u; i < GPU_CORE_MESART_DEFAULT_UBO_BYTES / 4u; ++i)
        fragment_words[i] = 0u;
    /* vec2 u_translation = (0.25, -0.125), leaving a visibly shifted
     * triangle; vec4 u_colour = unmistakable orange against the teal seed.
     * Values are exact IEEE
     * literals so the host compiler is never asked to encode test data. */
    /* Full-target compositor geometry is already transformed by its
     * viewport.  Applying the standalone test's translation as well pushes
     * the right edge outside the render target and enters X1-85's currently
     * unsupported clipping path. */
    words[0] = identity_transform ? 0u : 0x3e800000u;
    words[1] = identity_transform ? 0u : 0xbe000000u;
    words[2] = 0u;
    words[3] = 0u;
    fragment_words[0] = 0x3f800000u;
    fragment_words[1] = pipeline->fragment_texture_count ? 0x3f800000u : 0x3e19999au;
    fragment_words[2] = pipeline->fragment_texture_count ? 0x3f800000u : 0x3ca3d70au;
    fragment_words[3] = 0x3f800000u;
    asm_dma_clean_range((uint8_t *)g_mesart_uniform_pool.cpu + resource_offset,
                        GPU_CORE_MESART_FRAGMENT_UBO_OFFSET +
                            GPU_CORE_MESART_DEFAULT_UBO_BYTES);
    return 0;
}

static int gpu_core_mesart_configure_checker_texture(
    const mesart_renderer_graphics_pipeline *pipeline,
    adreno_x1_85_3d_draw *draw, uint32_t write_values)
{
    uint32_t *sampler;
    uint32_t *texture;
    uint32_t *pixels;

    if (!pipeline || !draw)
        return -1;
    if (!pipeline->fragment_texture_count)
        return 0;
    if (pipeline->vertex_texture_count != 0u ||
        pipeline->fragment_texture_count != 1u ||
        pipeline->reflection.vertex_sampler_count != 0u ||
        pipeline->reflection.fragment_sampler_count != 1u ||
        !g_mesart_uniform_pool.cpu ||
        GPU_CORE_MESART_TEXTURE_PIXELS_OFFSET +
            GPU_CORE_MESART_TEXTURE_BYTES > g_mesart_uniform_pool.size_bytes)
        return -2;

    draw->fragment_sampler_descriptor_gpu_va =
        GPU_CORE_MESART_UNIFORM_POOL_IOVA + GPU_CORE_MESART_SAMPLER_DESC_OFFSET;
    draw->fragment_texture_descriptor_gpu_va =
        GPU_CORE_MESART_UNIFORM_POOL_IOVA + GPU_CORE_MESART_TEXTURE_DESC_OFFSET;
    draw->fragment_texture_pixels_gpu_va =
        GPU_CORE_MESART_UNIFORM_POOL_IOVA + GPU_CORE_MESART_TEXTURE_PIXELS_OFFSET;
    if (!write_values)
        return 0;

    sampler = (uint32_t *)((uint8_t *)g_mesart_uniform_pool.cpu +
                           GPU_CORE_MESART_SAMPLER_DESC_OFFSET);
    texture = (uint32_t *)((uint8_t *)g_mesart_uniform_pool.cpu +
                           GPU_CORE_MESART_TEXTURE_DESC_OFFSET);
    pixels = (uint32_t *)((uint8_t *)g_mesart_uniform_pool.cpu +
                          GPU_CORE_MESART_TEXTURE_PIXELS_OFFSET);
    for (uint32_t i = 0u; i < 4u; ++i)
        sampler[i] = 0u;
    for (uint32_t i = 0u; i < 16u; ++i)
        texture[i] = 0u;

    /* Mesa a6xx_descriptors.xml: nearest sampling, clamp-to-edge, and the
     * MIPFILTER_LINEAR_FAR bit which denotes no mip chain. */
    sampler[0] = (1u << 5) | (1u << 8) | (1u << 11);
    sampler[1] = 1u << 6;
    /* One linear 16x16 FMT6_8_8_8_8_UNORM texture with identity swizzle.
     * Width/height/depth fields contain their actual sizes on A7xx. */
    texture[0] = (0u << 4) | (1u << 7) | (2u << 10) | (3u << 13) |
                 (0x30u << 22);
    texture[1] = GPU_CORE_MESART_TEXTURE_WIDTH |
                 (GPU_CORE_MESART_TEXTURE_HEIGHT << 15);
    texture[2] = (GPU_CORE_MESART_TEXTURE_PITCH << 7) | (1u << 29);
    texture[3] = 0u;
    texture[4] = (uint32_t)draw->fragment_texture_pixels_gpu_va;
    texture[5] = (uint32_t)(draw->fragment_texture_pixels_gpu_va >> 32) |
                 (1u << 17);
    for (uint32_t y = 0u; y < GPU_CORE_MESART_TEXTURE_HEIGHT; ++y)
    {
        for (uint32_t x = 0u; x < GPU_CORE_MESART_TEXTURE_WIDTH; ++x)
        {
            /* Four-pixel cyan/magenta checks make interpolation and actual
             * texel fetch visibly different from the previous flat shader. */
            pixels[y * GPU_CORE_MESART_TEXTURE_WIDTH + x] =
                (((x >> 2) ^ (y >> 2)) & 1u) ? 0xffff00ffu : 0xff00ffffu;
        }
    }
    asm_dma_clean_range(sampler, 16u);
    asm_dma_clean_range(texture, 64u);
    asm_dma_clean_range(pixels, GPU_CORE_MESART_TEXTURE_BYTES);
    return 0;
}

static int gpu_core_render_texture_iova(const gpu_render_texture *texture,
                                        uint64_t *out_gpu_va)
{
    gpu_render_target target;

    if (!texture || !out_gpu_va || !texture->surface.generation)
        return -1;
    target = (gpu_render_target){
        .surface = texture->surface,
        .cpu_pixels = (void *)texture->cpu_pixels,
        .cpu_bytes = texture->cpu_bytes,
        .width = texture->width,
        .height = texture->height,
        .stride_bytes = texture->stride_bytes,
        .format = texture->format,
    };
    return gpu_core_render_target_iova(&target, out_gpu_va);
}

static int gpu_core_mesart_configure_texture(
    const mesart_renderer_graphics_pipeline *pipeline,
    const gpu_render_texture *source, adreno_x1_85_3d_draw *draw,
    uint32_t diagnostic_probe, const gpu_render_rect *source_rect,
    uint32_t resource_slot)
{
    gpu_render_target backing_target;
    gpu_buffer *backing = 0;
    uint32_t *sampler;
    uint32_t *texture;
    uint64_t pixels_iova;
    uint64_t layer_bytes;
    uint64_t first_leaf = 0u;
    uint64_t last_leaf = 0u;
    uint32_t descriptor_width;
    uint32_t descriptor_height;
    uint64_t resource_offset = (uint64_t)resource_slot *
                               GPU_CORE_MESART_RESOURCE_STRIDE;

    if (!pipeline || !source || !draw ||
        pipeline->fragment_texture_count != 1u ||
        source->format != GPU_RENDER_FORMAT_BGRX8888 ||
        resource_slot >= GPU_CORE_MESART_RESOURCE_SLOTS ||
        resource_offset + GPU_CORE_MESART_TEXTURE_DESC_OFFSET + 64u >
            g_mesart_uniform_pool.size_bytes ||
        !source->width || !source->height ||
        source->width > 0x7fffu || source->height > 0x3fffu ||
        source->stride_bytes != source->width * 4u ||
        (source->stride_bytes & 255u) ||
        gpu_core_render_texture_iova(source, &pixels_iova) != 0)
        return -1;
    descriptor_width = diagnostic_probe ? 256u : source->width;
    descriptor_height = diagnostic_probe ? 256u : source->height;
    backing_target = (gpu_render_target){
        .surface = source->surface,
        .cpu_pixels = (void *)source->cpu_pixels,
        .cpu_bytes = source->cpu_bytes,
        .width = source->width,
        .height = source->height,
        .stride_bytes = source->stride_bytes,
        .format = source->format,
    };
    if (gpu_render_backend_surface_backing(&backing_target, &backing) != 0 ||
        !backing || gpu_iommu_query_page(&g_iommu_domain, pixels_iova,
                                         &first_leaf) != 0 ||
        gpu_iommu_query_page(&g_iommu_domain,
            pixels_iova + source->cpu_bytes - 1u, &last_leaf) != 0)
        return -1;
    if (diagnostic_probe)
    {
        terminal_print("[K:GPU] compositor source PA/leaf first,last=");
        terminal_print_inline_hex64(backing->phys);
        terminal_print("/");
        terminal_print_inline_hex64(first_leaf);
        terminal_print("/");
        terminal_print_inline_hex64(last_leaf);
        terminal_flush_log();
    }
    /* No speculative shader fetch against an absent or privileged-only
     * source leaf.  CP's earlier read probe cannot prove texture access. */
    if ((first_leaf & 0x43u) != 0x43u ||
        (last_leaf & 0x43u) != 0x43u ||
        (first_leaf & 0x0000fffffffff000ull) != backing->phys ||
        (last_leaf & 0x0000fffffffff000ull) !=
            backing->phys + ((source->cpu_bytes - 1u) & ~0xfffull))
        return -1;
    if (diagnostic_probe)
    {
        uint64_t mapped_bytes = (source->cpu_bytes + GPU_IOMMU_PAGE_SIZE - 1u) &
                                ~(GPU_IOMMU_PAGE_SIZE - 1u);
        for (uint64_t offset = 0u; offset < mapped_bytes;
             offset += GPU_IOMMU_PAGE_SIZE)
        {
            uint64_t leaf = 0u;
            if (gpu_iommu_query_page(&g_iommu_domain, pixels_iova + offset,
                                     &leaf) != 0 ||
                (leaf & 0x43u) != 0x43u ||
                (leaf & 0x0000fffffffff000ull) != backing->phys + offset)
                return -1;
        }
        terminal_print("[K:GPU] compositor source: every IOMMU page validated");
        terminal_flush_log();
    }
    layer_bytes = ((uint64_t)source->stride_bytes * source->height +
                   GPU_IOMMU_PAGE_SIZE - 1u) &
                  ~(GPU_IOMMU_PAGE_SIZE - 1u);
    if (!layer_bytes || layer_bytes > source->cpu_bytes +
            GPU_IOMMU_PAGE_SIZE - 1u ||
        (layer_bytes >> 12) > 0x7fffffu ||
        source->stride_bytes > 0x3fffffu || (pixels_iova & 63u))
        return -1;
    draw->fragment_sampler_descriptor_gpu_va =
        GPU_CORE_MESART_UNIFORM_POOL_IOVA + resource_offset + GPU_CORE_MESART_SAMPLER_DESC_OFFSET;
    draw->fragment_texture_descriptor_gpu_va =
        GPU_CORE_MESART_UNIFORM_POOL_IOVA + resource_offset + GPU_CORE_MESART_TEXTURE_DESC_OFFSET;
    draw->fragment_texture_pixels_gpu_va = pixels_iova;
    sampler = (uint32_t *)((uint8_t *)g_mesart_uniform_pool.cpu + resource_offset +
                           GPU_CORE_MESART_SAMPLER_DESC_OFFSET);
    texture = (uint32_t *)((uint8_t *)g_mesart_uniform_pool.cpu + resource_offset +
                           GPU_CORE_MESART_TEXTURE_DESC_OFFSET);
    for (uint32_t i = 0u; i < 4u; ++i)
        sampler[i] = 0u;
    for (uint32_t i = 0u; i < 16u; ++i)
        texture[i] = 0u;
    sampler[0] = (1u << 5) | (1u << 8) | (1u << 11);
    sampler[1] = 1u << 6;
    /* PIPE_FORMAT_B8G8R8X8_UNORM on a linear A7xx image. Mesa selects WXYZ
     * byte swap and forces the absent alpha channel to one. */
    texture[0] = A7XX_TEX_SWIZ_X(A7XX_TEX_X) |
                 A7XX_TEX_SWIZ_Y(A7XX_TEX_Y) |
                 A7XX_TEX_SWIZ_Z(A7XX_TEX_Z) |
                 A7XX_TEX_SWIZ_W(A7XX_TEX_ONE) |
                 A7XX_TEX_FORMAT(A7XX_TEX_FMT_8_8_8_8_UNORM) |
                 A7XX_TEX_SWAP(A7XX_TEX_SWAP_WXYZ);
    texture[1] = A7XX_TEX_WIDTH(descriptor_width) |
                 A7XX_TEX_HEIGHT(descriptor_height);
    /* Match fdl6_layout()/fdl6_view_init() for a linear four-byte format.
     * Mesa aligns its pitch to 256 bytes and encodes log2(256 / 64) = 2 in
     * PITCHALIGN even for a single-level 2D view.  Zero happened to survive
     * the tiny 16x16 checker, but is not a valid description of desktop-size
     * rows and can leave HLSQ/SP/VPC/RB waiting after the texture fetch. */
    texture[2] = A7XX_TEX_PITCH_ALIGN(2u) |
                 A7XX_TEX_PITCH(source->stride_bytes) |
                 A7XX_TEX_TYPE(A7XX_TEX_TYPE_2D);
    /* Mesa's fd6_view_init() always supplies the page-aligned layer size in
     * ARRAY_PITCH, including a single-level, single-layer 2D view.  The tiny
     * 16x16 bring-up texture happened to tolerate zero here, but leaving this
     * field absent makes TPL1's layer address calculation undefined once the
     * texture spans multiple pages. */
    texture[3] = A7XX_TEX_ARRAY_PITCH(layer_bytes);
    texture[4] = (uint32_t)pixels_iova;
    texture[5] = ((uint32_t)(pixels_iova >> 32) & 0x1ffffu) |
                 A7XX_TEX_DEPTH(1u);
    asm_dma_clean_range(sampler, 16u);
    asm_dma_clean_range(texture, 64u);
    if (!source->device_coherent && source_rect && source_rect->width &&
        source_rect->height)
    {
        for (uint32_t y = source_rect->y;
             y < source_rect->y + source_rect->height; ++y)
            asm_dma_clean_range((const uint8_t *)source->cpu_pixels +
                                    (uint64_t)y * source->stride_bytes +
                                    (uint64_t)source_rect->x * 4u,
                                (uint64_t)source_rect->width * 4u);
    }
    else if (!source->device_coherent)
        asm_dma_clean_range(source->cpu_pixels, source->cpu_bytes);
    if (diagnostic_probe)
    {
        terminal_print("[K:GPU] compositor texture IOVA/dim/pitch=");
        terminal_print_inline_hex64(pixels_iova);
        terminal_print_inline_hex64(((uint64_t)descriptor_width << 32) |
                                    descriptor_height);
        terminal_print_inline_hex64(source->stride_bytes);
        terminal_print("[K:GPU] compositor texture descriptor 0..5=");
        for (uint32_t i = 0u; i < 6u; ++i)
            terminal_print_inline_hex64(texture[i]);
        terminal_flush_log();
    }
    return 0;
}

int gpu_core_render_target_iova(const gpu_render_target *target,
                                uint64_t *out_gpu_va)
{
    gpu_buffer *surface_buffer = 0;
    uint64_t gpu_va;
    int surface_rc;

    if (!target || !out_gpu_va || !target->cpu_pixels || !target->cpu_bytes ||
        !target->width || !target->height)
        return -1;
    *out_gpu_va = 0u;

    /* The boot framebuffer was mapped during discovery. It is the only
     * external CPU allocation admitted as an early render target. */
    if (!target->surface.generation)
    {
        if (target->cpu_pixels != g_scanout_target.buffer.cpu ||
            target->cpu_bytes > g_scanout_target.buffer.size_bytes ||
            target->width != g_scanout_target.width ||
            target->height != g_scanout_target.height ||
            target->stride_bytes != g_scanout_target.pitch ||
            !g_scanout_target.buffer.iova)
            return -2;
        *out_gpu_va = g_scanout_target.buffer.iova;
        return 0;
    }

    surface_rc = gpu_render_backend_surface_backing(target, &surface_buffer);
    if (surface_rc != 0 || !surface_buffer || !surface_buffer->cpu ||
        !surface_buffer->phys || !surface_buffer->size_bytes ||
        surface_buffer->size_bytes > GPU_CORE_RENDER_SURFACE_SLOT_BYTES)
        return -3;
    gpu_va = GPU_CORE_RENDER_SURFACE_VA_BASE +
             (uint64_t)target->surface.slot * GPU_CORE_RENDER_SURFACE_SLOT_BYTES;
    if (gpu_va < GPU_CORE_RENDER_SURFACE_VA_BASE ||
        gpu_va >= GPU_CORE_RENDER_SURFACE_VA_END)
        return -4;
    if (surface_buffer->iova)
    {
        if (surface_buffer->iova != gpu_va)
            return -5;
        *out_gpu_va = gpu_va;
        return 0;
    }
    if (g_mesart_3d_inflight.active || g_scheduler.job_count ||
        g_gpu_smmu_bound_stream_count != ADRENO_X1_85_DRIVER_SID_COUNT ||
        !(g_primary.caps.flags & GPU_CAP_DMA_ISOLATED) ||
        !g_iommu_domain.root_phys ||
        /* Render-backend targets are consumed by RB, not just by CP.  Keep
         * their privilege attribute identical to the first scanout target so
         * a future offscreen surface has the same valid colour-store path. */
        gpu_iommu_map_buffer_with_attributes(&g_iommu_domain, surface_buffer,
                                             gpu_va, 0u) != 0)
        return -6;
    gpu_iommu_domain_prepare_for_device(&g_iommu_domain);
    if (gpu_smmuv2_invalidate_context(&g_gpu_smmu_window, &g_gpu_smmu_caps,
                                      g_gpu_smmu_context_bank) != 0)
    {
        /* No job is allowed during the admission-only map. Leave the buffer
         * unusable rather than handing a possibly stale mapping to a later
         * draw; reboot reconstructs this intentionally simple first domain. */
        surface_buffer->iova = 0u;
        return -7;
    }
    *out_gpu_va = gpu_va;
    return 0;
}

int gpu_core_mesart_3d_preflight(
    const mesart_renderer_graphics_pipeline *pipeline, uint32_t *out_dwords)
{
    gpu_render_target target;
    adreno_x1_85_3d_draw draw;
    int rc;

    if (out_dwords)
        *out_dwords = 0u;
    if (!pipeline || !out_dwords || g_mesart_3d_inflight.active ||
        !g_runtime_ring.buffer.cpu ||
        !g_scanout_target.buffer.cpu || !g_scanout_target.buffer.iova ||
        !g_scanout_target.width || !g_scanout_target.height ||
        !g_scanout_target.pitch ||
        g_scanout_target.pixel_format != GPU_CORE_GOP_BGRX_8888)
        return -1;
    target = (gpu_render_target){
        .surface = GPU_RENDER_EXTERNAL_SURFACE,
        .cpu_pixels = g_scanout_target.buffer.cpu,
        .cpu_bytes = g_scanout_target.buffer.size_bytes,
        .width = g_scanout_target.width,
        .height = g_scanout_target.height,
        .stride_bytes = g_scanout_target.pitch,
        .format = GPU_RENDER_FORMAT_BGRX8888,
    };
    draw = (adreno_x1_85_3d_draw){
        .pipeline = pipeline,
        .target = &target,
        .target_gpu_va = g_scanout_target.buffer.iova,
    };
    if (gpu_core_mesart_configure_vertex_buffer(pipeline, &draw, 0u, 0u, 0,
                                                 0u, 0u, 0u) != 0)
        return -2;
    if (gpu_core_mesart_configure_default_uniforms(pipeline, &draw, 0u, 0u, 0u) != 0)
        return -3;
    if (gpu_core_mesart_configure_checker_texture(pipeline, &draw, 0u) != 0)
        return -4;
    /* This uses the private runtime-ring allocation as a bounded scratch
     * encoder buffer only. It never seals, binds, writes a CP pointer or
     * acquires GX, so preflight has no hardware side effect. */
    if (gpu_ring_reset(&g_runtime_ring) != 0)
        return -2;
    rc = adreno_x1_85_3d_emit_sysmem_prologue(&g_runtime_ring);
    if (rc != 0)
    {
        (void)gpu_ring_reset(&g_runtime_ring);
        return -100 + rc;
    }
    rc = adreno_x1_85_3d_emit_x1e_baseline(&g_runtime_ring);
    if (rc != 0)
    {
        (void)gpu_ring_reset(&g_runtime_ring);
        return -110 + rc;
    }
    rc = adreno_x1_85_3d_emit_draw_state(&g_runtime_ring, &draw);
    if (rc != 0)
    {
        (void)gpu_ring_reset(&g_runtime_ring);
        return -120 + rc;
    }
    rc = adreno_x1_85_emit_auto_triangle_draw(&g_runtime_ring);
    if (rc != 0)
    {
        (void)gpu_ring_reset(&g_runtime_ring);
        return -130 + rc;
    }
    *out_dwords = g_runtime_ring.write_dwords;
    (void)gpu_ring_reset(&g_runtime_ring);
    return 0;
}

int gpu_core_mesart_3d_kick(
    const mesart_renderer_graphics_pipeline *pipeline, uint64_t *out_fence)
{
    gpu_render_target target;
    adreno_x1_85_3d_draw draw;
    const gpu_firmware_blob *sqe;
    gpu_cp_bind_config cp_config;
    gpu_cp_snapshot cp = {0};
    uint32_t gx_ack = 0u;
    uint32_t completion_magic;
    uint32_t completion_markers[9];
    uint64_t scanout_probe_iova;
    uint8_t submitted = 0u;
    int rc;

    if (out_fence)
        *out_fence = 0u;
    if (!pipeline || !out_fence || !g_runtime_backend_ready ||
        g_mesart_3d_inflight.active || g_scheduler.job_count ||
        !g_runtime_ring.buffer.iova ||
        !g_cp_pwrup.iova || !g_cp_shadow.iova || !g_cp_completion.cpu ||
        !g_cp_completion.iova || !g_scanout_target.buffer.cpu ||
        !g_scanout_target.buffer.iova || !g_scanout_target.width ||
        !g_scanout_target.height || !g_scanout_target.pitch ||
        g_scanout_target.width < 3u || g_scanout_target.height < 3u ||
        g_scanout_target.pixel_format != GPU_CORE_GOP_BGRX_8888)
        return -1;
    /* A small linear render surface keeps RB writes in normal kernel RAM.
     * Present only after the fence and CPU invalidation in poll(). */
    if (!g_triangle_target.cpu_pixels)
    {
        const gpu_render_surface_desc desc = {256u, 256u,
                                               GPU_RENDER_FORMAT_BGRX8888};
        gpu_render_surface_handle surface;
        if (g_scanout_target.width < desc.width ||
            g_scanout_target.height < desc.height ||
            gpu_render_surface_create(&desc, &surface) != 0)
            return -2;
        if (gpu_render_surface_target(surface, &g_triangle_target) != 0)
            return -2;
    }
    if (!g_triangle_target_iova &&
        gpu_core_render_target_iova(&g_triangle_target,
                                    &g_triangle_target_iova) != 0)
        return -2;
    if (!g_colour_write_probe.cpu_pixels)
    {
        const gpu_render_surface_desc desc = {16u, 16u, GPU_RENDER_FORMAT_BGRX8888};
        gpu_render_surface_handle surface;
        if (gpu_render_surface_create(&desc, &surface) != 0 ||
            gpu_render_surface_target(surface, &g_colour_write_probe) != 0)
            return -2;
    }
    if (g_colour_write_probe.stride_bytes != 64u ||
        g_colour_write_probe.cpu_bytes < 1024u)
        return -2;
    if (!g_colour_write_probe_iova &&
        gpu_core_render_target_iova(&g_colour_write_probe,
                                    &g_colour_write_probe_iova) != 0)
        return -2;
    for (uint32_t i = 0u; i < 256u; ++i)
        ((uint32_t *)g_colour_write_probe.cpu_pixels)[i] = GPU_CORE_MESART_SCANOUT_PROBE_MARKER;
    asm_dma_clean_range(g_colour_write_probe.cpu_pixels, 1024u);
    target = g_triangle_target;
    draw = (adreno_x1_85_3d_draw){
        .pipeline = pipeline,
        .target = &target,
        .target_gpu_va = g_triangle_target_iova,
    };
    if (gpu_core_mesart_configure_vertex_buffer(pipeline, &draw, 1u, 0u, 0,
                                                 0u, 0u, 0u) != 0)
        return -2;
    if (gpu_core_mesart_configure_default_uniforms(pipeline, &draw, 1u, 0u, 0u) != 0)
        return -3;
    if (gpu_core_mesart_configure_checker_texture(pipeline, &draw, 1u) != 0)
        return -3;
    if (adreno_x1_85_3d_validate_draw(&draw) != 0)
        return -4;
    /* Keep a source-level build witness next to the first hardware draw. A
     * failed visual test is otherwise indistinguishable from booting an older
     * USB kernel. Revision 18 records independent VPC/TSE/RB counters around
     * the draw, replacing the old stream-out query which cannot measure a
     * pass with stream-out intentionally disabled. */
    terminal_print("[K:GPU] Mesart A7xx draw-state revision=52 (kernel checker texture + async visual submit)");
    /* Authenticate before allowing CP's secure-mode transition. An unreadable
     * trust register is not evidence that a secure firmware load is needed. */
    {
        uint32_t trust = 0u;
        if (gpu_mmio_try_read32(&g_gpu_regs_window,
                ADRENO_X1_85_RBBM_SECVID_TRUST_CNTL, &trust) != 0 || trust > 1u) {
            terminal_error("[K:GPU] Draw withheld: secure-mode register unavailable");
            terminal_flush_log();
            return -114;
        }
        if (trust && gpu_zap_start(&g_firmware) != 0) {
            terminal_error("[K:GPU] Draw withheld: zap authentication/start failed (see PAS stage above)");
            terminal_flush_log();
            return -114;
        }
    }
    sqe = gpu_firmware_find(&g_firmware, GPU_FIRMWARE_SQE);
    if (!sqe || !gpu_firmware_payload_iova(sqe))
        return -4;
    if (gpu_ring_reset(&g_runtime_ring) != 0)
        return -5;
    ++g_runtime_3d_sequence;
    if (!g_runtime_3d_sequence)
        ++g_runtime_3d_sequence;
    completion_magic = GPU_CORE_RUNTIME_COMPLETION_MAGIC ^
                       (uint32_t)g_runtime_3d_sequence;
    if (!completion_magic)
        completion_magic = GPU_CORE_RUNTIME_COMPLETION_MAGIC;
    /* Centre screen is strictly inside the fixed test triangle.  Seed a
     * small, unmistakable BGRX marker before CPU cache clean; after RB_DONE
     * the completion poll can prove whether fragment output overwrote it.
     * Keeping the witness in the target avoids trusting a separate scratch
     * allocation or a CP-only completion as evidence of visible pixels. */
    g_mesart_3d_inflight.scanout_probe_marker =
        GPU_CORE_MESART_SCANOUT_PROBE_MARKER;
    g_mesart_3d_inflight.scanout_probe_changed = 0u;
    g_mesart_3d_inflight.scanout_probe_after = 0u;
    scanout_probe_iova = g_triangle_target_iova +
                         (uint64_t)(g_triangle_target.height / 2u) *
                             g_triangle_target.stride_bytes +
                         (uint64_t)(g_triangle_target.width / 2u) * 4u;
    /* Seed every pixel: the result check measures the entire allocation. */
    for (uint32_t i = 0u; i < target.width * target.height; ++i)
        ((uint32_t *)target.cpu_pixels)[i] = GPU_CORE_MESART_SCANOUT_PROBE_MARKER;
    for (uint32_t probe_y = 0u; probe_y < 3u; ++probe_y)
    {
        for (uint32_t probe_x = 0u; probe_x < 3u; ++probe_x)
        {
            uint32_t x = g_triangle_target.width / 2u + probe_x - 1u;
            uint32_t y = g_triangle_target.height / 2u + probe_y - 1u;
            uint8_t *pixel = (uint8_t *)g_triangle_target.cpu_pixels +
                             (uint64_t)y * g_triangle_target.stride_bytes +
                             (uint64_t)x * 4u;

            *(uint32_t *)pixel = GPU_CORE_MESART_SCANOUT_PROBE_MARKER;
        }
    }
    for (uint32_t i = 0u; i < GPU_CORE_MESART_DIAGNOSTIC_BYTES; ++i)
        ((uint8_t *)g_cp_completion.cpu)[i] = 0u;
    for (uint32_t i = 0u; i < sizeof(completion_markers) /
                              sizeof(completion_markers[0]); ++i)
    {
        /* Each value is per-submit and distinct, so a stale cache line or a
         * partially consumed 3D ring cannot masquerade as a completion. */
        completion_markers[i] = completion_magic ^ (0x00010101u * (i + 1u));
    }
    completion_markers[8] = completion_magic;
    gpu_buffer_prepare_for_device(&g_cp_completion);
    /* The CPU shell/desktop may have just used this framebuffer. Clean it
     * before RB renders, then invalidate it only after the RB completion
     * fence below. */
    asm_dma_clean_range(g_triangle_target.cpu_pixels,
                        g_triangle_target.cpu_bytes);
    if (!g_runtime_gx_lease_held)
    {
        rc = gpu_gmu_gen7_acquire_gpu(&g_gpu_gmu_window, &gx_ack);
        if (rc != 0)
            return -5;
        g_runtime_gx_lease_held = 1u;
    }
    if (gpu_cp_status_read(&g_gpu_regs_window, adreno_x1_85_cp_layout(),
                             &cp) != 0 || cp.hw_fault || cp.protect_status)
    {
        rc = -6;
        goto out;
    }
    if (adreno_x1_85_emit_minimal_cp_init(&g_runtime_ring,
                                          g_cp_pwrup.iova) != 0 ||
        adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
                                           g_cp_completion.iova,
                                           completion_markers[0]) != 0 ||
        /* CP initialization does NOT leave SECVID secure mode. Request the
         * normal firmware-mediated transition, never force TRUST_CNTL from
         * EL1. This relies on platform-resident zap support; staging its file
         * alone does not authenticate it. A missing handler is bounded by
         * the existing asynchronous timeout, with markers isolating it. */
        adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
            g_cp_completion.iova + GPU_CORE_MESART_SECURE_BEGIN_OFFSET,
            0x53454330u) != 0 ||
        adreno_x1_85_emit_nonsecure_transition(&g_runtime_ring) != 0 ||
        adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
            g_cp_completion.iova + GPU_CORE_MESART_SECURE_END_OFFSET,
            0x53454331u) != 0 ||
        adreno_x1_85_3d_emit_sysmem_prologue(&g_runtime_ring) != 0 ||
        adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
                                           g_cp_completion.iova + 4u,
                                           completion_markers[1]) != 0 ||
        adreno_x1_85_3d_emit_x1e_baseline(&g_runtime_ring) != 0 ||
        adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
                                           g_cp_completion.iova + 8u,
                                           completion_markers[2]) != 0 ||
        adreno_x1_85_3d_emit_draw_state(&g_runtime_ring, &draw) != 0 ||
        adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
                                           g_cp_completion.iova + 12u,
                                           completion_markers[3]) != 0 ||
        /* Read back the state that decides whether the front end can launch
         * this draw.  These ranges are Mesa a6xx.xml dword offsets and are
         * intentionally diagnostic-only: shader bases, RT base, VFD system
         * values, shader enable/instruction size, and RB render routing. */
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0xa81cu, 2u,
            g_cp_completion.iova + GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0xa983u, 2u,
            g_cp_completion.iova + GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET + 8u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x8825u, 2u,
            g_cp_completion.iova + GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET + 16u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0xa000u, 2u,
            g_cp_completion.iova + GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET + 24u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0xa823u, 2u,
            g_cp_completion.iova + GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET + 32u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0xab04u, 2u,
            g_cp_completion.iova + GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET + 40u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x8800u, 2u,
            g_cp_completion.iova + GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET + 48u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x8812u, 1u,
            g_cp_completion.iova + GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET + 56u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x88e4u, 1u,
            g_cp_completion.iova + GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET + 60u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0xa010u, 4u,
            g_cp_completion.iova + GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET + 64u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0xa090u, 2u,
            g_cp_completion.iova + GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET + 80u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0xa0d0u, 1u,
            g_cp_completion.iova + GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET + 88u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0xa989u, 1u,
            g_cp_completion.iova + GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET + 92u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x8865u, 1u,
            g_cp_completion.iova + GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET + 96u) != 0 ||
        /* Keep a separate, contiguous capture of the actual RT0 attachment
         * and FS output routing.  The previous summary established that RB
         * ran; these exact dwords distinguish a missing store enable from a
         * store directed at an inherited surface. */
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x8820u, 8u,
            g_cp_completion.iova + GPU_CORE_MESART_RT_STATE_SNAPSHOT_OFFSET) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0xa98bu, 12u,
            g_cp_completion.iova + GPU_CORE_MESART_RT_STATE_SNAPSHOT_OFFSET + 32u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x88e5u, 1u,
            g_cp_completion.iova + GPU_CORE_MESART_CCU_SNAPSHOT_OFFSET) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x8e07u, 1u,
            g_cp_completion.iova + GPU_CORE_MESART_CCU_SNAPSHOT_OFFSET + 4u) != 0 ||
        adreno_x1_85_3d_emit_stage_counter_select(&g_runtime_ring) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x03fau, 8u,
            g_cp_completion.iova + GPU_CORE_MESART_UFC_START_OFFSET) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x0036u, 1u,
            g_cp_completion.iova + GPU_CORE_MESART_RBBM_START_OFFSET) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x035cu, 10u,
            g_cp_completion.iova + GPU_CORE_MESART_CCU_START_OFFSET) != 0 ||
        /* Register snapshots are pairs of low/high dwords.  VPC says whether
         * primitive assembly happened; TSE says whether it reached
         * rasterization; RB says whether fragments executed and stored. */
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x0350u, 4u,
            g_cp_completion.iova + GPU_CORE_MESART_STAGE_COUNTER_START_OFFSET) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x0366u, 4u,
            g_cp_completion.iova + GPU_CORE_MESART_STAGE_COUNTER_START_OFFSET + 16u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x03d6u, 4u,
            g_cp_completion.iova + GPU_CORE_MESART_STAGE_COUNTER_START_OFFSET + 32u) != 0 ||
        /* Deliberately bracket the first draw with two RB completion events.
         * The pre-draw event determines whether direct-sysmem RB completion
         * works at all; the post-draw event then isolates an actual draw/state
         * stall without relying on CP's independent ring read pointer. */
        adreno_x1_85_emit_rb_done_fence(&g_runtime_ring,
                                         g_cp_completion.iova + 16u,
                                         completion_markers[4]) != 0 ||
        adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
                                           g_cp_completion.iova + 20u,
                                           completion_markers[5]) != 0 ||
        adreno_x1_85_emit_auto_triangle_draw(&g_runtime_ring) != 0 ||
        adreno_x1_85_3d_emit_sysmem_fini(&g_runtime_ring) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x03fau, 8u,
            g_cp_completion.iova + GPU_CORE_MESART_UFC_END_OFFSET) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x0036u, 1u,
            g_cp_completion.iova + GPU_CORE_MESART_RBBM_DRAW_OFFSET) != 0 ||
        /* Pre-draw readback cannot detect draw-time replay of stale groups.
         * Capture exactly the same RT/fragment routing range after the draw. */
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x8820u, 8u,
            g_cp_completion.iova + GPU_CORE_MESART_RT_POST_DRAW_OFFSET) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0xa98bu, 12u,
            g_cp_completion.iova + GPU_CORE_MESART_RT_POST_DRAW_OFFSET + 32u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x035cu, 10u,
            g_cp_completion.iova + GPU_CORE_MESART_CCU_END_OFFSET) != 0 ||
        /* The CPU witness below can be stale on a non-coherent mapping.
         * Read the same centre pixel through CP after the RT flush into the
         * private coherent completion page, proving what the GPU observes. */
        adreno_x1_85_emit_cp_memory_copy_u32(
            &g_runtime_ring,
            g_cp_completion.iova + GPU_CORE_MESART_RT_GPU_READBACK_OFFSET,
            scanout_probe_iova) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x0350u, 4u,
            g_cp_completion.iova + GPU_CORE_MESART_STAGE_COUNTER_END_OFFSET) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x0366u, 4u,
            g_cp_completion.iova + GPU_CORE_MESART_STAGE_COUNTER_END_OFFSET + 16u) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x03d6u, 4u,
            g_cp_completion.iova + GPU_CORE_MESART_STAGE_COUNTER_END_OFFSET + 32u) != 0 ||
        adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
                                           g_cp_completion.iova + 24u,
                                           completion_markers[6]) != 0 ||
        /* Only after capturing every shader result: test another allocation.
         * CP stores establish mapping visibility; the 2D engine exercises
         * colour writes without the signed vertex/fragment programs. */
        adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
            g_colour_write_probe_iova, 0xc0dec0deu) != 0 ||
        adreno_x1_85_emit_cp_memory_copy_u32(&g_runtime_ring,
            g_cp_completion.iova + GPU_CORE_MESART_CP_WRITE_PROBE_OFFSET,
            g_colour_write_probe_iova) != 0 ||
        adreno_x1_85_emit_solid_fill_probe(&g_runtime_ring,
            g_colour_write_probe_iova) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x03fau, 8u,
            g_cp_completion.iova + GPU_CORE_MESART_UFC_BLIT_OFFSET) != 0 ||
        adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x0036u, 1u,
            g_cp_completion.iova + GPU_CORE_MESART_RBBM_BLIT_OFFSET) != 0 ||
        adreno_x1_85_emit_cp_memory_copy_u32(&g_runtime_ring,
            g_cp_completion.iova + GPU_CORE_MESART_BLIT_READBACK_OFFSET,
            g_colour_write_probe_iova + 8u * 64u + 8u * 4u) != 0 ||
        adreno_x1_85_emit_rb_done_fence(&g_runtime_ring,
                                         g_cp_completion.iova + 28u,
                                         completion_markers[7]) != 0 ||
        adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
                                           g_cp_completion.iova + 32u,
                                           completion_markers[8]) != 0 ||
        gpu_ring_seal(&g_runtime_ring) != 0)
    {
        rc = -7;
        goto out;
    }
    cp_config = (gpu_cp_bind_config){
        gpu_firmware_payload_iova(sqe),
        g_runtime_ring.buffer.iova,
        g_cp_shadow.iova + GPU_CORE_CP_BR_RPTR_OFFSET,
        g_cp_shadow.iova + GPU_CORE_CP_BV_RPTR_OFFSET,
        (uint32_t)g_runtime_ring.buffer.size_bytes,
        ADRENO_X1_85_CP_RB_CNTL_BOOT,
        0u,
        ADRENO_X1_85_CP_BR_APRIV_MASK,
        ADRENO_X1_85_CP_AUX_APRIV_MASK,
        ADRENO_X1_85_CP_AUX_APRIV_MASK,
    };
    if (gpu_cp_bind(&g_gpu_regs_window, adreno_x1_85_cp_layout(),
                    &cp_config) != 0)
    {
        rc = -8;
        goto out;
    }
    g_runtime_ring_bound = 1u;
    rc = gpu_cp_submit_fenced(&g_gpu_regs_window, &g_gpu_gmu_window,
                              ADRENO_X1_85_GMU_AHB_FENCE_STATUS,
                              adreno_x1_85_cp_layout(), &g_runtime_ring);
    if (rc != 0)
    {
        rc = -9;
        goto out;
    }
    submitted = 1u;
    g_mesart_3d_inflight.fence = g_runtime_3d_sequence;
    g_mesart_3d_inflight.started_counter = gpu_core_monotonic_counter();
    g_mesart_3d_inflight.expected_rptr = g_runtime_ring.write_dwords;
    for (uint32_t i = 0u; i < sizeof(completion_markers) /
                              sizeof(completion_markers[0]); ++i)
        g_mesart_3d_inflight.completion_markers[i] = completion_markers[i];
    /* Set this only after the fenced write-pointer publication succeeded: it
     * prevents another kernel client from resetting the one live ring while
     * CP/RB still own it. */
    g_mesart_3d_inflight.active = 1u;
    *out_fence = g_runtime_3d_sequence;
    return 0;

out:
    if (rc != 0 && (submitted || g_runtime_gx_lease_held))
    {
        /* Fail closed after a real CP/runtime error.  A successful trusted
         * runtime keeps the known-good GX lease, but an error must not leave
         * an uncertain front-end powered for a later caller. */
        (void)gpu_gmu_gen7_release_gpu(&g_gpu_gmu_window);
        g_runtime_gx_lease_held = 0u;
        g_runtime_ring_bound = 0u;
        g_runtime_backend_ready = 0u;
    }
    return rc;
}

static int gpu_core_compositor_emit_remaining_draws(
    const adreno_x1_85_3d_draw *draws, uint32_t count)
{
    if (!draws || !count)
        return 0;
    for (uint32_t i = 1u; i < count; ++i)
        if (adreno_x1_85_3d_emit_draw_state(&g_runtime_ring, &draws[i]) != 0 ||
            adreno_x1_85_emit_auto_triangle_list_draw(
                &g_runtime_ring, draws[i].vertex_count) != 0)
            return -1;
    return 0;
}

static int gpu_core_compositor_present_layers_kick(
    const mesart_renderer_graphics_pipeline *pipeline,
    const gpu_core_compositor_layer *layers, uint32_t layer_count,
    const gpu_render_target *target, uint32_t diagnostic_probe,
    uint64_t *out_fence)
{
    adreno_x1_85_3d_draw draws[GPU_RENDER_MAX_BATCH_DRAWS];
    const gpu_render_texture *source;
    const gpu_firmware_blob *sqe;
    gpu_cp_bind_config cp_config;
    gpu_cp_snapshot cp = {0};
    uint32_t gx_ack = 0u;
    uint32_t markers[9];
    uint32_t magic;
    uint64_t target_iova = 0u;
    uint64_t target_first_leaf = 0u;
    uint64_t target_last_leaf = 0u;
    gpu_buffer *target_backing = 0;
    uint8_t bind_runtime;
    uint8_t submitted = 0u;
    int rc;

    if (out_fence)
        *out_fence = 0u;
    if (!pipeline || !layers || !layer_count ||
        layer_count > GPU_RENDER_MAX_BATCH_DRAWS || !target ||
        (diagnostic_probe && layer_count != 1u) ||
        !g_runtime_backend_ready ||
        g_mesart_3d_inflight.active || g_scheduler.job_count ||
        !g_runtime_ring.buffer.iova || !g_cp_pwrup.iova ||
        !g_cp_shadow.iova || !g_cp_completion.cpu || !g_cp_completion.iova ||
        !target->cpu_pixels ||
        target->format != GPU_RENDER_FORMAT_BGRX8888 ||
        gpu_core_render_target_iova(target, &target_iova) != 0)
        return -1;
    source = layers[0].source;
    for (uint32_t i = 0u; i < layer_count; ++i)
    {
        const gpu_render_texture *layer_source = layers[i].source;
        const gpu_render_rect *source_rect = &layers[i].source_rect;
        const gpu_render_rect *destination_rect = &layers[i].destination_rect;
        if (!layer_source || !source_rect->width || !source_rect->height ||
            !destination_rect->width || !destination_rect->height ||
            source_rect->x >= layer_source->width ||
            source_rect->y >= layer_source->height ||
            source_rect->width > layer_source->width - source_rect->x ||
            source_rect->height > layer_source->height - source_rect->y ||
            destination_rect->x >= target->width ||
            destination_rect->y >= target->height ||
            destination_rect->width > target->width - destination_rect->x ||
            destination_rect->height > target->height - destination_rect->y ||
            source_rect->width != destination_rect->width ||
            source_rect->height != destination_rect->height)
            return -1;
    }
    if (target->surface.generation)
    {
        if (gpu_render_backend_surface_backing(target, &target_backing) != 0)
            return -1;
    }
    else
        target_backing = &g_scanout_target.buffer;
    if (!target_backing ||
        gpu_iommu_query_page(&g_iommu_domain, target_iova,
                             &target_first_leaf) != 0 ||
        gpu_iommu_query_page(&g_iommu_domain,
            target_iova + target->cpu_bytes - 1u,
            &target_last_leaf) != 0 ||
        (target_first_leaf & 0x43u) != 0x43u ||
        (target_last_leaf & 0x43u) != 0x43u ||
        (target_first_leaf & 0x0000fffffffff000ull) !=
            target_backing->phys ||
        (target_last_leaf & 0x0000fffffffff000ull) !=
            target_backing->phys +
            ((target->cpu_bytes - 1u) & ~0xfffull))
        return -1;
    if (diagnostic_probe)
    {
        terminal_print("[K:GPU] compositor target IOVA/PA/leaf first,last=");
        terminal_print_inline_hex64(target_iova);
        terminal_print("/");
        terminal_print_inline_hex64(target_backing->phys);
        terminal_print("/");
        terminal_print_inline_hex64(target_first_leaf);
        terminal_print("/");
        terminal_print_inline_hex64(target_last_leaf);
        terminal_flush_log();
    }
    for (uint32_t i = 0u; i < layer_count; ++i)
    {
        const gpu_render_texture *layer_source = layers[i].source;
        draws[i] = (adreno_x1_85_3d_draw){
            .pipeline = pipeline,
            .target = target,
            .target_gpu_va = target_iova,
            .vertex_count = 6u,
            .viewport_x = layers[i].destination_rect.x,
            .viewport_y = layers[i].destination_rect.y,
            .viewport_width = layers[i].destination_rect.width,
            .viewport_height = layers[i].destination_rect.height,
            .expand_safe_viewport = 1u,
        };
        if (gpu_core_mesart_configure_vertex_buffer(
                pipeline, &draws[i], 1u, 4u, &layers[i].source_rect,
                layer_source->width, layer_source->height, i) != 0 ||
            gpu_core_mesart_configure_default_uniforms(
                pipeline, &draws[i], 1u, 1u, i) != 0 ||
            gpu_core_mesart_configure_texture(
                pipeline, layer_source, &draws[i], diagnostic_probe,
                &layers[i].source_rect, i) != 0 ||
            adreno_x1_85_3d_validate_draw(&draws[i]) != 0)
            return -2;
    }
    {
        uint32_t trust = 0u;
        if (gpu_mmio_try_read32(&g_gpu_regs_window,
                ADRENO_X1_85_RBBM_SECVID_TRUST_CNTL, &trust) != 0 ||
            trust > 1u || (trust && gpu_zap_start(&g_firmware) != 0))
            return -3;
    }
    sqe = gpu_firmware_find(&g_firmware, GPU_FIRMWARE_SQE);
    if (!sqe || !gpu_firmware_payload_iova(sqe))
        return -4;
    ++g_runtime_3d_sequence;
    if (!g_runtime_3d_sequence)
        ++g_runtime_3d_sequence;
    magic = GPU_CORE_RUNTIME_COMPLETION_MAGIC ^
            (uint32_t)g_runtime_3d_sequence;
    if (!magic)
        magic = GPU_CORE_RUNTIME_COMPLETION_MAGIC;
    for (uint32_t i = 0u; i < 9u; ++i)
        markers[i] = magic ^ (0x00010101u * (i + 1u));
    markers[8] = magic;
    /* Normal compositor frames use the unique ordered marker at word 8 as
     * their completion fence. Clearing and cleaning the whole diagnostic
     * page every frame was bring-up scaffolding. */
    if (diagnostic_probe)
    {
        for (uint32_t i = 0u; i < GPU_CORE_MESART_DIAGNOSTIC_BYTES; ++i)
            ((uint8_t *)g_cp_completion.cpu)[i] = 0u;
        gpu_buffer_prepare_for_device(&g_cp_completion);
    }
    if (!g_runtime_gx_lease_held)
    {
        rc = gpu_gmu_gen7_acquire_gpu(&g_gpu_gmu_window, &gx_ack);
        if (rc != 0)
            return -5;
        g_runtime_gx_lease_held = 1u;
    }
    bind_runtime = !g_runtime_ring_bound;
    if (gpu_cp_status_read(&g_gpu_regs_window, adreno_x1_85_cp_layout(),
                           &cp) != 0 || cp.hw_fault || cp.protect_status ||
        (g_runtime_ring_bound && cp.rb_rptr != cp.rb_wptr) ||
        (g_runtime_ring_bound &&
         cp.rb_rptr >= g_runtime_ring.capacity_dwords))
    {
        rc = -6;
        goto compositor_out;
    }
    /* CP_RB_RPTR is circular state owned by the hardware.  Rebuilding each
     * batch at dword zero while merely rebinding the same 32 KiB allocation
     * appeared to work for eight ~0x3db-dword submissions, then failed on
     * the ninth as CP's hidden wrap phase crossed the 8192-dword boundary.
     * Append at the actual idle pointer and allow PM4 packets to wrap. */
    if (gpu_ring_reset_at(&g_runtime_ring,
                          g_runtime_ring_bound ? cp.rb_rptr : 0u) != 0)
    {
        rc = -6;
        goto compositor_out;
    }
    /* The boot probe executes CP_ME_INIT before the first trusted draw, but
     * gpu_zap_start() may perform PAS AUTH_AND_RESET immediately above.  A
     * fresh runtime-ring bind therefore has to restore CP's context after
     * authentication.  Do this only for the first bind: appended batches
     * retain the live context and must not repeat either CP_ME_INIT or the
     * secure-to-nonsecure WFI/WFM handoff while graphics state is sticky. */
    if ((bind_runtime &&
         adreno_x1_85_emit_minimal_cp_init(&g_runtime_ring,
                                           g_cp_pwrup.iova) != 0) ||
        (diagnostic_probe && adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
            g_cp_completion.iova, markers[0]) != 0) ||
        (bind_runtime &&
         adreno_x1_85_emit_nonsecure_transition(&g_runtime_ring) != 0) ||
        (diagnostic_probe && adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
            g_cp_completion.iova + 4u, markers[1]) != 0) ||
        /* Prove that CP can translate both ends of the pre-attached desktop
         * surface before TPL1 interprets it as a texture.  These copies are
         * diagnostics only and never feed the draw. */
        (diagnostic_probe && adreno_x1_85_emit_cp_memory_copy_u32(&g_runtime_ring,
            g_cp_completion.iova + GPU_CORE_COMPOSITOR_SOURCE_FIRST_OFFSET,
            draws[0].fragment_texture_pixels_gpu_va) != 0) ||
        (diagnostic_probe && adreno_x1_85_emit_cp_memory_copy_u32(&g_runtime_ring,
            g_cp_completion.iova + GPU_CORE_COMPOSITOR_SOURCE_LAST_OFFSET,
            draws[0].fragment_texture_pixels_gpu_va + source->stride_bytes *
                (uint64_t)source->height - 4u) != 0) ||
        (diagnostic_probe && adreno_x1_85_emit_cp_memory_copy_u32(&g_runtime_ring,
            g_cp_completion.iova + GPU_CORE_COMPOSITOR_DESCRIPTOR_OFFSET,
            draws[0].fragment_texture_descriptor_gpu_va) != 0) ||
        (diagnostic_probe && adreno_x1_85_emit_cp_memory_copy_u32(&g_runtime_ring,
            g_cp_completion.iova + GPU_CORE_COMPOSITOR_DESCRIPTOR_OFFSET + 4u,
            draws[0].fragment_texture_descriptor_gpu_va + 8u) != 0) ||
        (diagnostic_probe && adreno_x1_85_emit_cp_memory_copy_u32(&g_runtime_ring,
            g_cp_completion.iova + GPU_CORE_COMPOSITOR_DESCRIPTOR_OFFSET + 8u,
            draws[0].fragment_texture_descriptor_gpu_va + 12u) != 0) ||
        (diagnostic_probe && adreno_x1_85_emit_cp_memory_copy_u32(&g_runtime_ring,
            g_cp_completion.iova + GPU_CORE_COMPOSITOR_DESCRIPTOR_OFFSET + 12u,
            draws[0].fragment_texture_descriptor_gpu_va + 16u) != 0) ||
        (diagnostic_probe && adreno_x1_85_emit_cp_memory_copy_u32(&g_runtime_ring,
            g_cp_completion.iova + GPU_CORE_COMPOSITOR_DESCRIPTOR_OFFSET + 16u,
            draws[0].fragment_texture_descriptor_gpu_va + 20u) != 0) ||
        adreno_x1_85_3d_emit_sysmem_prologue(&g_runtime_ring) != 0 ||
        (bind_runtime && adreno_x1_85_3d_emit_x1e_baseline(&g_runtime_ring) != 0) ||
        (diagnostic_probe && adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
            g_cp_completion.iova + 8u, markers[2]) != 0) ||
        adreno_x1_85_3d_emit_draw_state(&g_runtime_ring, &draws[0]) != 0 ||
        (diagnostic_probe && adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
            g_cp_completion.iova + 12u, markers[3]) != 0) ||
        /* CP register-to-memory snapshots serialize against live graphics
         * blocks.  They are useful for a one-shot diagnostic, but on the
         * persistent compositor ring the next half-tile can wait forever
         * reading TSE/RB counters from the preceding draw. */
        (diagnostic_probe &&
         adreno_x1_85_3d_emit_stage_counter_select(&g_runtime_ring) != 0) ||
        /* Capture a CP-ordered baseline.  The counters are free-running, so
         * their live absolute values cannot identify activity by themselves. */
        (diagnostic_probe &&
         adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x0350u, 4u,
            g_cp_completion.iova + GPU_CORE_MESART_STAGE_COUNTER_START_OFFSET) != 0) ||
        (diagnostic_probe &&
         adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x0366u, 4u,
            g_cp_completion.iova + GPU_CORE_MESART_STAGE_COUNTER_START_OFFSET + 16u) != 0) ||
        (diagnostic_probe &&
         adreno_x1_85_emit_register_snapshot(&g_runtime_ring, 0x03d6u, 4u,
            g_cp_completion.iova + GPU_CORE_MESART_STAGE_COUNTER_START_OFFSET + 32u) != 0) ||
        (diagnostic_probe && adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
            g_cp_completion.iova + 16u, markers[4]) != 0) ||
        (diagnostic_probe && adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
            g_cp_completion.iova + 20u, markers[5]) != 0) ||
        (draws[0].rectangle_list ?
         adreno_x1_85_emit_auto_rectangle_draw(&g_runtime_ring) :
         adreno_x1_85_emit_auto_triangle_list_draw(&g_runtime_ring,
                                                    draws[0].vertex_count)) != 0 ||
        gpu_core_compositor_emit_remaining_draws(draws, layer_count) != 0 ||
        /* The first two post-draw markers locate failures around the event
         * fence.  They are not themselves render-completion fences. */
        (diagnostic_probe && adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
            g_cp_completion.iova + 24u, markers[6]) != 0) ||
        (diagnostic_probe && adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
            g_cp_completion.iova + 28u, markers[7]) != 0) ||
        /* Publish marker[8] only after Mesa's complete A7xx sysmem cache
         * epilogue has retired on the GPU command stream. */
        adreno_x1_85_3d_emit_sysmem_fini_async(&g_runtime_ring,
            g_cp_completion.iova + 32u, markers[8]) != 0 ||
        gpu_ring_seal(&g_runtime_ring) != 0)
    {
        rc = -7;
        goto compositor_out;
    }
    cp_config = (gpu_cp_bind_config){
        gpu_firmware_payload_iova(sqe),
        g_runtime_ring.buffer.iova,
        g_cp_shadow.iova + GPU_CORE_CP_BR_RPTR_OFFSET,
        g_cp_shadow.iova + GPU_CORE_CP_BV_RPTR_OFFSET,
        (uint32_t)g_runtime_ring.buffer.size_bytes,
        ADRENO_X1_85_CP_RB_CNTL_BOOT,
        0u,
        ADRENO_X1_85_CP_BR_APRIV_MASK,
        ADRENO_X1_85_CP_AUX_APRIV_MASK,
        ADRENO_X1_85_CP_AUX_APRIV_MASK,
    };
    if (bind_runtime &&
        gpu_cp_bind(&g_gpu_regs_window, adreno_x1_85_cp_layout(),
                    &cp_config) != 0)
    {
        rc = -8;
        goto compositor_out;
    }
    g_runtime_ring_bound = 1u;
    rc = gpu_cp_submit_fenced(&g_gpu_regs_window, &g_gpu_gmu_window,
                              ADRENO_X1_85_GMU_AHB_FENCE_STATUS,
                              adreno_x1_85_cp_layout(), &g_runtime_ring);
    if (rc != 0)
    {
        rc = -9;
        goto compositor_out;
    }
    submitted = 1u;
    g_mesart_3d_inflight = (gpu_core_mesart_3d_inflight){0};
    g_mesart_3d_inflight.fence = g_runtime_3d_sequence;
    g_mesart_3d_inflight.started_counter = gpu_core_monotonic_counter();
    g_mesart_3d_inflight.expected_rptr = g_runtime_ring.write_dwords;
    for (uint32_t i = 0u; i < 9u; ++i)
        g_mesart_3d_inflight.completion_markers[i] = markers[i];
    g_mesart_3d_inflight.source_first_expected =
        *(const uint32_t *)source->cpu_pixels;
    g_mesart_3d_inflight.source_last_expected =
        *(const uint32_t *)((const uint8_t *)source->cpu_pixels +
            (uint64_t)source->stride_bytes * source->height - 4u);
    {
        const uint32_t *descriptor = (const uint32_t *)
            ((const uint8_t *)g_mesart_uniform_pool.cpu +
             GPU_CORE_MESART_TEXTURE_DESC_OFFSET);
        g_mesart_3d_inflight.descriptor_expected[0] = descriptor[0];
        g_mesart_3d_inflight.descriptor_expected[1] = descriptor[2];
        g_mesart_3d_inflight.descriptor_expected[2] = descriptor[3];
        g_mesart_3d_inflight.descriptor_expected[3] = descriptor[4];
        g_mesart_3d_inflight.descriptor_expected[4] = descriptor[5];
    }
    g_mesart_3d_inflight.compositor_present = 1u;
    g_mesart_3d_inflight.compositor_triangle_half = 3u;
    g_mesart_3d_inflight.active = 1u;
    *out_fence = g_runtime_3d_sequence;
    return 0;

compositor_out:
    if (submitted || g_runtime_gx_lease_held)
    {
        (void)gpu_gmu_gen7_release_gpu(&g_gpu_gmu_window);
        g_runtime_gx_lease_held = 0u;
        g_runtime_ring_bound = 0u;
        g_runtime_backend_ready = 0u;
    }
    return rc;
}

int gpu_core_compositor_present_kick(
    const mesart_renderer_graphics_pipeline *pipeline,
    const gpu_render_texture *source, const gpu_render_target *target,
    const gpu_render_rect *source_rect,
    const gpu_render_rect *destination_rect,
    uint32_t triangle_half, uint32_t diagnostic_probe, uint64_t *out_fence)
{
    gpu_core_compositor_layer layer;

    if (!source || !source_rect || !destination_rect || triangle_half > 3u)
        return -1;
    layer = (gpu_core_compositor_layer){source, *source_rect, *destination_rect};
    return gpu_core_compositor_present_layers_kick(
        pipeline, &layer, 1u, target, diagnostic_probe, out_fence);
}

int gpu_core_compositor_present_batch_kick(
    const mesart_renderer_graphics_pipeline *pipeline,
    const gpu_core_compositor_layer *layers, uint32_t layer_count,
    const gpu_render_target *target, uint64_t *out_fence)
{
    return gpu_core_compositor_present_layers_kick(
        pipeline, layers, layer_count, target, 0u, out_fence);
}

static int gpu_core_mesart_3d_fail(int rc)
{
    gpu_cp_snapshot failure_cp = {0};
    gpu_smmuv2_context_fault render_fault = {0};
    gpu_smmuv2_global_fault global_fault = {0};
    uint32_t engine_status[4] = {0};
    uint64_t live_counters[6] = {0};
    uint8_t live_counter_valid[6] = {0};
    const uint32_t engine_offsets[4] = {
        ADRENO_X1_85_RBBM_STATUS_GEN7,
        ADRENO_X1_85_RBBM_STATUS1_GEN7,
        ADRENO_X1_85_RBBM_STATUS2_GEN7,
        ADRENO_X1_85_RBBM_STATUS3_GEN7,
    };

    terminal_error("[K:GPU] Mesart 3D stall rc=");
    terminal_print_inline_hex64((uint64_t)(uint32_t)(-rc));
    terminal_print(" elapsed counter/rate=");
    terminal_print_inline_hex64(gpu_core_monotonic_counter() -
                                g_mesart_3d_inflight.started_counter);
    terminal_print("/");
    terminal_print_inline_hex64(gpu_core_monotonic_frequency());
    terminal_print(" RBBM status 0..3=");
    for (uint32_t i = 0u; i < 4u; ++i)
    {
        if (gpu_mmio_try_read32(&g_gpu_regs_window, engine_offsets[i],
                                &engine_status[i]) == 0)
            terminal_print_inline_hex64(engine_status[i]);
        else
            terminal_print("unreadable");
        terminal_print("/");
    }
    if (g_mesart_3d_inflight.compositor_present)
    {
        const uint32_t counter_offsets[6] = {
            0x0350u, 0x0352u, 0x0366u, 0x0368u, 0x03d6u, 0x03d8u,
        };
        terminal_print(" live VPC/TSE/RB counters=");
        for (uint32_t i = 0u; i < 6u; ++i)
        {
            uint32_t lo = 0u;
            uint32_t hi = 0u;
            /* Perf-counter XML addresses are GPU dword indices; the CPU
             * MMIO accessor takes byte offsets. */
            if (gpu_mmio_try_read32(&g_gpu_regs_window,
                                    counter_offsets[i] * 4u, &lo) == 0 &&
                gpu_mmio_try_read32(&g_gpu_regs_window,
                                    (counter_offsets[i] + 1u) * 4u, &hi) == 0)
            {
                live_counters[i] = ((uint64_t)hi << 32) | lo;
                live_counter_valid[i] = 1u;
                terminal_print_inline_hex64(live_counters[i]);
            }
            else
                terminal_print("unreadable");
            terminal_print("/");
        }
    }
    if (g_cp_completion.cpu) {
        asm_dma_invalidate_range(g_cp_completion.cpu, GPU_CORE_MESART_DIAGNOSTIC_BYTES);
        const uint32_t *words = (const uint32_t *)g_cp_completion.cpu;
        if (g_mesart_3d_inflight.compositor_present)
        {
            terminal_print(" counter baseline/delta=");
            for (uint32_t i = 0u; i < 6u; ++i)
            {
                uint32_t word = GPU_CORE_MESART_STAGE_COUNTER_START_OFFSET /
                                4u + i * 2u;
                uint64_t baseline = ((uint64_t)words[word + 1u] << 32) |
                                    words[word];
                terminal_print_inline_hex64(baseline);
                terminal_print("/");
                if (live_counter_valid[i])
                    terminal_print_inline_hex64(live_counters[i] - baseline);
                else
                    terminal_print("unreadable");
                terminal_print("/");
            }
        }
        terminal_print(" secure-transition begin/end=");
        terminal_print_inline_hex64(words[GPU_CORE_MESART_SECURE_BEGIN_OFFSET / 4u]);
        terminal_print("/");
        terminal_print_inline_hex64(words[GPU_CORE_MESART_SECURE_END_OFFSET / 4u]);
        if (words[GPU_CORE_MESART_SECURE_BEGIN_OFFSET / 4u] == 0x53454330u &&
            words[GPU_CORE_MESART_SECURE_END_OFFSET / 4u] != 0x53454331u)
            terminal_error("[K:GPU] Non-secure transition did not finish; platform zap support may be unavailable");
        terminal_print(" completion stages 0..8=");
        for (uint32_t i = 0u; i < 9u; ++i)
            terminal_print_inline_hex64(words[i]);
        if (g_mesart_3d_inflight.compositor_present)
        {
            terminal_print(" compositor triangle half=");
            terminal_print_inline_hex64(
                g_mesart_3d_inflight.compositor_triangle_half);
            terminal_print(" compositor source GPU/CPU first=");
            terminal_print_inline_hex64(
                words[GPU_CORE_COMPOSITOR_SOURCE_FIRST_OFFSET / 4u]);
            terminal_print("/");
            terminal_print_inline_hex64(
                g_mesart_3d_inflight.source_first_expected);
            terminal_print(" last=");
            terminal_print_inline_hex64(
                words[GPU_CORE_COMPOSITOR_SOURCE_LAST_OFFSET / 4u]);
            terminal_print("/");
            terminal_print_inline_hex64(
                g_mesart_3d_inflight.source_last_expected);
            terminal_print(" compositor descriptor GPU/CPU 0,2,3,4,5=");
            for (uint32_t i = 0u; i < 5u; ++i)
            {
                terminal_print_inline_hex64(words[
                    GPU_CORE_COMPOSITOR_DESCRIPTOR_OFFSET / 4u + i]);
                terminal_print("/");
                terminal_print_inline_hex64(
                    g_mesart_3d_inflight.descriptor_expected[i]);
            }
        }
    }
    if (gpu_cp_status_read(&g_gpu_regs_window,
                             adreno_x1_85_cp_layout(), &failure_cp) == 0)
    {
        terminal_print(" CP-fault=");
        terminal_print_inline_hex64(failure_cp.hw_fault);
        terminal_print(" protect=");
        terminal_print_inline_hex64(failure_cp.protect_status);
        terminal_print(" rptr=");
        terminal_print_inline_hex64(failure_cp.rb_rptr);
        terminal_print(" wptr=");
        terminal_print_inline_hex64(failure_cp.rb_wptr);
        if (g_runtime_ring.buffer.cpu && g_runtime_ring.write_dwords)
        {
            const uint32_t *ring =
                (const uint32_t *)g_runtime_ring.buffer.cpu;
            uint32_t begin = failure_cp.rb_rptr > 8u ?
                failure_cp.rb_rptr - 8u : 0u;
            uint32_t end = failure_cp.rb_rptr + 8u;
            if (end > g_runtime_ring.write_dwords)
                end = g_runtime_ring.write_dwords;
            terminal_print(" ring near rptr=");
            for (uint32_t i = begin; i < end; ++i)
            {
                terminal_print_inline_hex64(i);
                terminal_print(":");
                terminal_print_inline_hex64(ring[i]);
            }
        }
    }
    if (gpu_smmuv2_read_context_fault(&g_gpu_smmu_window, &g_gpu_smmu_caps,
                                      g_gpu_smmu_context_bank,
                                      &render_fault) == 0)
    {
        terminal_print(" CB0-FSR=");
        terminal_print_inline_hex64(render_fault.fsr);
        if (render_fault.fsr & GPU_SMMUV2_FSR_FAULT_MASK)
        {
            uint64_t leaf = 0u;
            terminal_print(" active-fault FAR=");
            terminal_print_inline_hex64(render_fault.fault_address);
            terminal_print(" FSYNR0=");
            terminal_print_inline_hex64(render_fault.fsynr0);
            terminal_print(" CPU leaf=");
            if (gpu_iommu_query_page(&g_iommu_domain,
                    render_fault.fault_address, &leaf) == 0)
                terminal_print_inline_hex64(leaf);
            else
                terminal_print("unmapped");
        }
        else
            terminal_print(" (no active context fault; FAR/FSYNR stale)");
    }
    if (gpu_smmuv2_read_global_fault(&g_gpu_smmu_window, &global_fault) == 0)
    {
        terminal_print(" SMMU-GFSR=");
        terminal_print_inline_hex64(global_fault.gfsr);
    }
    terminal_print("");
    g_mesart_3d_inflight = (gpu_core_mesart_3d_inflight){0};
    (void)gpu_gmu_gen7_release_gpu(&g_gpu_gmu_window);
    g_runtime_gx_lease_held = 0u;
    g_runtime_ring_bound = 0u;
    g_runtime_backend_ready = 0u;
    return rc;
}

int gpu_core_mesart_3d_poll(uint64_t *out_fence)
{
    uint32_t rptr = 0u;
    uint64_t elapsed;
    uint64_t timeout;

    if (out_fence)
        *out_fence = 0u;
    if (!g_mesart_3d_inflight.active)
        return 1;
    if (gpu_mmio_try_read32(&g_gpu_regs_window,
                            adreno_x1_85_cp_layout()->rb_rptr,
                            &rptr) != 0)
        return gpu_core_mesart_3d_fail(-10);
    elapsed = gpu_core_monotonic_counter() -
        g_mesart_3d_inflight.started_counter;
    timeout = gpu_core_monotonic_frequency() *
        (g_mesart_3d_inflight.compositor_present ?
            GPU_CORE_COMPOSITOR_TIMEOUT_SECONDS :
            GPU_CORE_MESART_3D_TIMEOUT_SECONDS);
    if (rptr != g_mesart_3d_inflight.expected_rptr)
    {
        if ((++g_mesart_3d_inflight.poll_count & 63u) == 0u)
        {
            gpu_smmuv2_context_fault fault = {0};
            if (gpu_smmuv2_read_context_fault(&g_gpu_smmu_window,
                    &g_gpu_smmu_caps, g_gpu_smmu_context_bank,
                    &fault) == 0 &&
                (fault.fsr & GPU_SMMUV2_FSR_FAULT_MASK))
                return gpu_core_mesart_3d_fail(-110);
        }
        if (elapsed >= timeout)
            return gpu_core_mesart_3d_fail(-10);
        return 1;
    }
    /* RB_DONE_TS follows CP's rptr.  Checking it once per frame avoids the
     * old busy wait while retaining the stronger proof that the draw reached
     * the render backend, rather than merely being parsed by CP. */
    asm_dma_invalidate_range(g_cp_completion.cpu,
                             GPU_CORE_MESART_DIAGNOSTIC_BYTES);
    if ((g_mesart_3d_inflight.compositor_present &&
         ((uint32_t *)g_cp_completion.cpu)[8] !=
             g_mesart_3d_inflight.completion_markers[8]) ||
        (!g_mesart_3d_inflight.compositor_present &&
         ((uint32_t *)g_cp_completion.cpu)[7] == 0u))
    {
        if (elapsed >= timeout)
            return gpu_core_mesart_3d_fail(-108);
        return 1;
    }
    for (uint32_t i = 0u; i < sizeof(g_mesart_3d_inflight.completion_markers) /
                              sizeof(g_mesart_3d_inflight.completion_markers[0]); ++i)
    {
        if (g_mesart_3d_inflight.compositor_present && i != 8u)
            continue;
        uint32_t rb_timestamp = !g_mesart_3d_inflight.compositor_present &&
            (i == 4u || i == 7u);
        if (rb_timestamp)
        {
            if (((uint32_t *)g_cp_completion.cpu)[i] == 0u)
                return gpu_core_mesart_3d_fail(-101 - (int)i);
            continue;
        }
        if (((uint32_t *)g_cp_completion.cpu)[i] !=
            g_mesart_3d_inflight.completion_markers[i])
            return gpu_core_mesart_3d_fail(-101 - (int)i);
    }
    if (g_mesart_3d_inflight.compositor_present)
    {
        gpu_smmuv2_context_fault fault = {0};
        gpu_smmuv2_global_fault global = {0};
        uint32_t trust = 1u;
        uint64_t completed = g_mesart_3d_inflight.fence;

        /* RBBM_STATUS and its undocumented detail banks are diagnostics,
         * not completion fences.  X1-85 keeps VPC_BUSY set after the very
         * first successful draw and later retains nonzero detail values;
         * requiring zero falsely stopped the compositor after four tiles
         * despite final CP markers and normal RB colour-write counters.
         * The ordered private-memory marker above is the submission fence;
         * reject only an actual trust or SMMU fault here. */
        if (gpu_mmio_try_read32(&g_gpu_regs_window,
                ADRENO_X1_85_RBBM_SECVID_TRUST_CNTL, &trust) != 0 ||
            trust != 0u ||
            gpu_smmuv2_read_context_fault(&g_gpu_smmu_window,
                &g_gpu_smmu_caps, g_gpu_smmu_context_bank, &fault) != 0 ||
            gpu_smmuv2_read_global_fault(&g_gpu_smmu_window, &global) != 0 ||
            (fault.fsr & GPU_SMMUV2_FSR_FAULT_MASK) || global.gfsr)
            return gpu_core_mesart_3d_fail(-115);
        if (out_fence)
            *out_fence = completed;
        g_mesart_3d_inflight = (gpu_core_mesart_3d_inflight){0};
        return 0;
    }
    /* CP/SMMU status alone misses GPU-side rejection of colour writes.
     * Keep stage-specific sticky status; do not clear or alter trust state
     * to hide an error. These RBBM bits are from Mesa's A7xx register XML. */
    {
        const uint32_t *words = (const uint32_t *)g_cp_completion.cpu;
        uint32_t before = words[GPU_CORE_MESART_RBBM_START_OFFSET / 4u];
        uint32_t draw = words[GPU_CORE_MESART_RBBM_DRAW_OFFSET / 4u];
        uint32_t blit = words[GPU_CORE_MESART_RBBM_BLIT_OFFSET / 4u];
        uint32_t trust = 0u;
        uint32_t host_status = 0u;
        uint32_t faults = 0u;
        /* CP snapshots can return the bus's invalid-read sentinel. Never
         * decode its set bits as hardware faults (rev41 did exactly that). */
        if (before != 0xdeafbeadu && before != 0xffffffffu) faults |= before;
        if (draw != 0xdeafbeadu && draw != 0xffffffffu) faults |= draw;
        if (blit != 0xdeafbeadu && blit != 0xffffffffu) faults |= blit;
        terminal_print("[K:GPU] RBBM interrupt status before/draw/blit=");
        terminal_print_inline_hex64(before);
        terminal_print("/"); terminal_print_inline_hex64(draw);
        terminal_print("/"); terminal_print_inline_hex64(blit);
        /* Host-only guarded read: this register must never be read through
         * an unprivileged command packet (CP protection rejects that). */
        if (before == 0xdeafbeadu || draw == 0xdeafbeadu || blit == 0xdeafbeadu)
            terminal_print(" (DEAFBEAD snapshots unavailable, not fault bits)");
        if (gpu_mmio_try_read32(&g_gpu_regs_window,
                ADRENO_X1_85_RBBM_INT_STATUS, &host_status) == 0 &&
            host_status != 0xdeafbeadu && host_status != 0xffffffffu) {
            terminal_print(" host-status=");
            terminal_print_inline_hex64(host_status);
            faults |= host_status;
        }
        int trust_valid = gpu_mmio_try_read32(&g_gpu_regs_window,
            ADRENO_X1_85_RBBM_SECVID_TRUST_CNTL, &trust) == 0 &&
            trust != 0xdeafbeadu && trust != 0xffffffffu;
        if (trust_valid) {
            terminal_print(" SECVID trust=");
            terminal_print_inline_hex64(trust);
        } else {
            terminal_print(" SECVID trust=unreadable");
        }
        if (faults & (1u << 28))
            terminal_error("[K:GPU] TSBWRITEERROR: GPU rejected a write against its trusted-memory policy");
        if (faults & (1u << 29))
            terminal_error("[K:GPU] SWFUSEVIOLATION: GPU reports a security-policy violation");
        const uint64_t *start = (const uint64_t *)((const uint8_t *)
            g_cp_completion.cpu + GPU_CORE_MESART_UFC_START_OFFSET);
        const uint64_t *end = (const uint64_t *)((const uint8_t *)
            g_cp_completion.cpu + GPU_CORE_MESART_UFC_END_OFFSET);
        const uint64_t *after_blit = (const uint64_t *)((const uint8_t *)
            g_cp_completion.cpu + GPU_CORE_MESART_UFC_BLIT_OFFSET);
        const char *names[] = {" write-data=", " write-requests=",
                               " incoming-writes=", " write-stalls="};
        terminal_print("[K:GPU] UFC draw/blit deltas");
        for (uint32_t i = 0u; i < 4u; ++i) {
            terminal_print(names[i]);
            terminal_print_inline_hex64(end[i] - start[i]);
            terminal_print("/");
            terminal_print_inline_hex64(after_blit[i] - end[i]);
        }
        terminal_flush_log();
        if (!trust_valid || trust != 0u) {
            terminal_error("[K:GPU] Non-secure mode not confirmed; refusing to report rendering success");
            return gpu_core_mesart_3d_fail(-113);
        }
        if (faults & ((1u << 28) | (1u << 29)))
            return gpu_core_mesart_3d_fail(-112);
    }
    /* A drained CP ring is not proof that a non-stalling DMA fault was absent. */
    {
        gpu_smmuv2_context_fault fault = {0};
        gpu_smmuv2_global_fault global = {0};
        if (gpu_smmuv2_read_context_fault(&g_gpu_smmu_window, &g_gpu_smmu_caps,
                g_gpu_smmu_context_bank, &fault) != 0 ||
            gpu_smmuv2_read_global_fault(&g_gpu_smmu_window, &global) != 0)
            return gpu_core_mesart_3d_fail(-109);
        if ((fault.fsr & GPU_SMMUV2_FSR_FAULT_MASK) || global.gfsr)
            return gpu_core_mesart_3d_fail(-110);
    }
    {
        const uint32_t *state = (const uint32_t *)((const uint8_t *)
            g_cp_completion.cpu + GPU_CORE_MESART_STATE_SNAPSHOT_OFFSET);

        terminal_print("[K:GPU] Mesart state-readback VS=");
        terminal_print_inline_hex64((uint64_t)state[0] |
                                    ((uint64_t)state[1] << 32));
        terminal_print(" PS=");
        terminal_print_inline_hex64((uint64_t)state[2] |
                                    ((uint64_t)state[3] << 32));
        terminal_print(" RT=");
        terminal_print_inline_hex64((uint64_t)state[4] |
                                    ((uint64_t)state[5] << 32));
        terminal_print(" VFD=");
        terminal_print_inline_hex64((uint64_t)state[6] |
                                    ((uint64_t)state[7] << 32));
        terminal_print(" VS-config/size=");
        terminal_print_inline_hex64((uint64_t)state[8] |
                                    ((uint64_t)state[9] << 32));
        terminal_print(" PS-config/size=");
        terminal_print_inline_hex64((uint64_t)state[10] |
                                    ((uint64_t)state[11] << 32));
        terminal_print(" RB-cntl/render=");
        terminal_print_inline_hex64((uint64_t)state[12] |
                                    ((uint64_t)state[13] << 32));
        terminal_print(" RB-buffer/clear=");
        terminal_print_inline_hex64((uint64_t)state[14] |
                                    ((uint64_t)state[15] << 32));
        terminal_print(" VBO=");
        terminal_print_inline_hex64((uint64_t)state[16] |
                                    ((uint64_t)state[17] << 32));
        terminal_print(" size/stride=");
        terminal_print_inline_hex64((uint64_t)state[18] |
                                    ((uint64_t)state[19] << 32));
        terminal_print(" fetch/dest=");
        terminal_print_inline_hex64((uint64_t)state[20] |
                                    ((uint64_t)state[22] << 32));
        terminal_print(" blend=");
        terminal_print_inline_hex64((uint64_t)state[23] |
                                    ((uint64_t)state[24] << 32));
        terminal_print("");
    }
    {
        const uint32_t *rt = (const uint32_t *)((const uint8_t *)
            g_cp_completion.cpu + GPU_CORE_MESART_RT_STATE_SNAPSHOT_OFFSET);

        terminal_print("[K:GPU] Mesart RT0 retained control/blend=");
        terminal_print_inline_hex64((uint64_t)rt[0] |
                                    ((uint64_t)rt[1] << 32));
        terminal_print(" buf/pitch=");
        terminal_print_inline_hex64((uint64_t)rt[2] |
                                    ((uint64_t)rt[3] << 32));
        terminal_print(" array/base=");
        terminal_print_inline_hex64((uint64_t)rt[4] |
                                    ((uint64_t)rt[5] << 32));
        terminal_print(" basehi/gmem=");
        terminal_print_inline_hex64((uint64_t)rt[6] |
                                    ((uint64_t)rt[7] << 32));
        terminal_print(" PS-mask/cntl=");
        terminal_print_inline_hex64((uint64_t)rt[8] |
                                    ((uint64_t)rt[9] << 32));
        terminal_print(" PS-mrt/output=");
        terminal_print_inline_hex64((uint64_t)rt[10] |
                                    ((uint64_t)rt[11] << 32));
        terminal_print(" PS-rtfmt=");
        terminal_print_inline_hex64(rt[19]);
        terminal_print("");
    }
    {
        const uint64_t *start = (const uint64_t *)((const uint8_t *)
            g_cp_completion.cpu + GPU_CORE_MESART_STAGE_COUNTER_START_OFFSET);
        const uint64_t *end = (const uint64_t *)((const uint8_t *)
            g_cp_completion.cpu + GPU_CORE_MESART_STAGE_COUNTER_END_OFFSET);

        terminal_print("[K:GPU] Mesart stages delta VPC-PC=");
        terminal_print_inline_hex64(end[0] - start[0]);
        terminal_print(" VPC-visible=");
        terminal_print_inline_hex64(end[1] - start[1]);
        terminal_print(" TSE-input=");
        terminal_print_inline_hex64(end[2] - start[2]);
        terminal_print(" TSE-visible=");
        terminal_print_inline_hex64(end[3] - start[3]);
        terminal_print(" RB-PS=");
        terminal_print_inline_hex64(end[4] - start[4]);
        terminal_print(" RB-C-write=");
        terminal_print_inline_hex64(end[5] - start[5]);
        terminal_print("");
    }
    {
        const uint64_t *start = (const uint64_t *)((const uint8_t *)
            g_cp_completion.cpu + GPU_CORE_MESART_CCU_START_OFFSET);
        const uint64_t *end = (const uint64_t *)((const uint8_t *)
            g_cp_completion.cpu + GPU_CORE_MESART_CCU_END_OFFSET);
        const char *names[5] = {" busy=", " colour-blocks=", " colour-hits=",
                                " GMEM-writes=", " colour-drops="};
        terminal_print("[K:GPU] Mesart CCU deltas");
        for (uint32_t i = 0; i < 5u; ++i) {
            terminal_print(names[i]);
            terminal_print_inline_hex64(end[i] - start[i]);
        }
    }
    {
        const uint32_t *before = (const uint32_t *)((const uint8_t *)
            g_cp_completion.cpu + GPU_CORE_MESART_RT_STATE_SNAPSHOT_OFFSET);
        const uint32_t *after = (const uint32_t *)((const uint8_t *)
            g_cp_completion.cpu + GPU_CORE_MESART_RT_POST_DRAW_OFFSET);
        uint32_t changed = 0u;
        for (uint32_t i = 0u; i < GPU_CORE_MESART_RT_STATE_SNAPSHOT_BYTES / 4u; ++i) {
            if (before[i] == after[i])
                continue;
            ++changed;
            terminal_print("[K:GPU] Mesart draw changed register=");
            terminal_print_inline_hex64(i < 8u ? 0x8820u + i : 0xa98bu + i - 8u);
            terminal_print(" before=");
            terminal_print_inline_hex64(before[i]);
            terminal_print(" after=");
            terminal_print_inline_hex64(after[i]);
        }
        terminal_print("[K:GPU] Mesart post-draw RT state changes=");
        terminal_print_inline_hex64(changed);
        terminal_print(" target=");
        terminal_print_inline_hex64((uint64_t)after[5] | ((uint64_t)after[6] << 32));
    }
    terminal_print("[K:GPU] Mesart CCU cache/control=");
    terminal_print_inline_hex64(*(const uint32_t *)((const uint8_t *)
        g_cp_completion.cpu + GPU_CORE_MESART_CCU_SNAPSHOT_OFFSET));
    terminal_print("/");
    terminal_print_inline_hex64(*(const uint32_t *)((const uint8_t *)
        g_cp_completion.cpu + GPU_CORE_MESART_CCU_SNAPSHOT_OFFSET + 4u));
    terminal_print("");
    {
        uint32_t green = 0u;
        asm_dma_invalidate_range(g_colour_write_probe.cpu_pixels, 1024u);
        for (uint32_t i = 0u; i < 256u; ++i)
            green += ((const uint32_t *)g_colour_write_probe.cpu_pixels)[i] == 0xff00ff00u;
        terminal_print("[K:GPU] Independent write test CP echo=");
        terminal_print_inline_hex64(*(const uint32_t *)((const uint8_t *)g_cp_completion.cpu +
            GPU_CORE_MESART_CP_WRITE_PROBE_OFFSET));
        terminal_print(" expected=C0DEC0DE; 2D GPU centre=");
        terminal_print_inline_hex64(*(const uint32_t *)((const uint8_t *)g_cp_completion.cpu +
            GPU_CORE_MESART_BLIT_READBACK_OFFSET));
        terminal_print(" green pixels=");
        terminal_print_inline_hex64(green);
        terminal_print("/256 (diagnostic only, not shader success)");
    }
    terminal_print("[K:GPU] Mesart RT GPU readback centre=");
    terminal_print_inline_hex64(*(const uint32_t *)((const uint8_t *)
        g_cp_completion.cpu + GPU_CORE_MESART_RT_GPU_READBACK_OFFSET));
    terminal_print(" marker=");
    terminal_print_inline_hex64(g_mesart_3d_inflight.scanout_probe_marker);
    terminal_print("");
    if (out_fence)
        *out_fence = g_mesart_3d_inflight.fence;
    asm_dma_invalidate_range(g_triangle_target.cpu_pixels,
                             g_triangle_target.cpu_bytes);
    for (uint32_t probe_y = 0u; probe_y < 3u; ++probe_y)
    {
        for (uint32_t probe_x = 0u; probe_x < 3u; ++probe_x)
        {
            uint32_t x = g_triangle_target.width / 2u + probe_x - 1u;
            uint32_t y = g_triangle_target.height / 2u + probe_y - 1u;
            uint8_t *pixel = (uint8_t *)g_triangle_target.cpu_pixels +
                             (uint64_t)y * g_triangle_target.stride_bytes +
                             (uint64_t)x * 4u;
            uint32_t value = *(uint32_t *)pixel;

            if (probe_x == 1u && probe_y == 1u)
                g_mesart_3d_inflight.scanout_probe_after = value;
            if (value != g_mesart_3d_inflight.scanout_probe_marker)
                ++g_mesart_3d_inflight.scanout_probe_changed;
        }
    }
    terminal_print("[K:GPU] Mesart offscreen pixel witness changed=");
    terminal_print_inline_hex64(g_mesart_3d_inflight.scanout_probe_changed);
    terminal_print("/9 centre=");
    terminal_print_inline_hex64(g_mesart_3d_inflight.scanout_probe_after);
    terminal_print(" marker=");
    terminal_print_inline_hex64(g_mesart_3d_inflight.scanout_probe_marker);
    terminal_print("");
    {
        const uint32_t *pixels = (const uint32_t *)g_triangle_target.cpu_pixels;
        uint32_t changed = 0u;
        const uint32_t count = g_triangle_target.width * g_triangle_target.height;
        for (uint32_t i = 0u; i < count; ++i)
            changed += pixels[i] != GPU_CORE_MESART_SCANOUT_PROBE_MARKER;
        terminal_print("[K:GPU] Mesart offscreen changed pixels=");
        terminal_print_inline_hex64(changed);
        terminal_print(" total=");
        terminal_print_inline_hex64(count);
        terminal_print("");
        if (changed)
        {
            /* Retain the completed offscreen surface. Presentation happens
             * after each normal KGFX frame, so the desktop remains live and
             * can use this shader result without racing the GPU fence. */
            g_mesart_3d_frame_ready = 1u;
            terminal_print("[K:GPU] Mesart GPU surface cached for asynchronous composition");
        }
        else
        {
            terminal_error("[K:GPU] Mesart draw completed without changing any target pixel");
            terminal_flush_log();
            g_mesart_3d_inflight = (gpu_core_mesart_3d_inflight){0};
            return -111;
        }
    }
    terminal_flush_log();
    g_mesart_3d_inflight = (gpu_core_mesart_3d_inflight){0};
    return 0;
}

int gpu_core_mesart_3d_present_cached(void)
{
    const uint32_t *pixels;
    uint32_t x;
    uint32_t y;

    if (!g_mesart_3d_frame_ready)
        return 1;
    if (!g_triangle_target.cpu_pixels || !g_scanout_target.buffer.cpu ||
        g_triangle_target.width > g_scanout_target.width ||
        g_triangle_target.height > g_scanout_target.height)
        return -1;
    pixels = (const uint32_t *)g_triangle_target.cpu_pixels;
    x = (g_scanout_target.width - g_triangle_target.width) / 2u;
    y = (g_scanout_target.height - g_triangle_target.height) / 2u;
    for (uint32_t row = 0u; row < g_triangle_target.height; ++row)
    {
        uint32_t *dst = (uint32_t *)((uint8_t *)g_scanout_target.buffer.cpu +
            (uint64_t)(y + row) * g_scanout_target.pitch + (uint64_t)x * 4u);
        const uint32_t *src = (const uint32_t *)((const uint8_t *)pixels +
            (uint64_t)row * g_triangle_target.stride_bytes);
        /* The diagnostic seed is the transparent portion of this first
         * offscreen profile. Only shader-written pixels cover KGFX. */
        for (uint32_t col = 0u; col < g_triangle_target.width; ++col)
            if (src[col] != GPU_CORE_MESART_SCANOUT_PROBE_MARKER)
                dst[col] = src[col];
        asm_dma_clean_range(dst, g_triangle_target.width * 4u);
    }
    return 0;
}

void gpu_core_mesart_3d_discard_cached(void)
{
    g_mesart_3d_frame_ready = 0u;
}

uint8_t gpu_core_mesart_3d_frame_ready(void)
{
    return g_mesart_3d_frame_ready;
}

uint8_t gpu_core_mesart_3d_busy(void)
{
    return g_mesart_3d_inflight.active;
}

int gpu_core_mesart_3d_submit(
    const mesart_renderer_graphics_pipeline *pipeline, uint64_t *out_fence)
{
    uint64_t fence = 0u;
    int rc;

    if (out_fence)
        *out_fence = 0u;
    if (!out_fence || (rc = gpu_core_mesart_3d_kick(pipeline, &fence)) != 0)
        return rc;
    for (uint32_t poll = 0u; poll < 100000u; ++poll)
    {
        uint64_t completed_fence = 0u;

        rc = gpu_core_mesart_3d_poll(&completed_fence);
        if (rc == 0)
        {
            *out_fence = completed_fence;
            return 0;
        }
        if (rc < 0)
            return rc;
        asm_relax();
    }
    return gpu_core_mesart_3d_fail(-10);
}

/* Keep the first runtime hardware path independently fail-closed.  Mesart
 * already checked this same grammar before sealing its private snapshot, but
 * the backend repeats the check at its final privilege boundary: a future
 * scheduler caller cannot accidentally turn a generic sealed ring into raw
 * GPU command authority. */
static uint32_t gpu_core_odd_parity(uint32_t value)
{
    value ^= value >> 4;
    value ^= value >> 8;
    value ^= value >> 16;
    return (0x9669u >> (value & 0x0fu)) & 1u;
}

static int gpu_core_validate_mesart_ring(const gpu_command_ring *ring,
                                         uint64_t writable_gpu_va,
                                         uint64_t writable_gpu_bytes,
                                         uint32_t policy_flags)
{
    const uint32_t *words;
    uint32_t at = 0u;

    if (!ring || !ring->sealed || !ring->buffer.cpu || !ring->write_dwords ||
        ring->write_dwords > ring->capacity_dwords)
        return -1;
    words = (const uint32_t *)ring->buffer.cpu;
    while (at < ring->write_dwords)
    {
        uint32_t word = words[at];
        uint32_t opcode;
        uint32_t payload_dwords;

        if ((word & 0xf0000000u) != 0x70000000u ||
            (word & 0x0f000000u) != 0u)
            return -2;
        opcode = (word >> 16) & 0x7fu;
        payload_dwords = word & 0x3fffu;
        if (((word >> 23) & 1u) != gpu_core_odd_parity(opcode) ||
            ((word >> 15) & 1u) != gpu_core_odd_parity(payload_dwords))
            return -3;
        if (payload_dwords > ring->write_dwords - at - 1u)
            return -4;
        if (opcode == 0x10u)
        {
            /* Inert CP_NOP packets remain valid in either policy. */
        }
        else if (opcode == 0x12u &&
                 (policy_flags & GPU_SCHEDULER_POLICY_A7XX_SYNC))
        {
            /* CP_WAIT_MEM_WRITES is an address-free ordering primitive.  The
             * exact zero-payload form is the only one accepted at this
             * privilege boundary. */
            if (payload_dwords != 0u)
                return -5;
        }
        else if (opcode == 0x3du &&
                 (policy_flags & GPU_SCHEDULER_POLICY_A7XX_RESOURCE_WRITE))
        {
            uint64_t destination;

            if (!writable_gpu_va || writable_gpu_bytes < sizeof(uint32_t) ||
                payload_dwords != 3u)
                return -6;
            destination = (uint64_t)words[at + 1u] |
                          ((uint64_t)words[at + 2u] << 32);
            if ((destination & 3u) || destination < writable_gpu_va ||
                destination - writable_gpu_va >
                    writable_gpu_bytes - sizeof(uint32_t))
                return -7;
        }
        else
            return -8;
        at += payload_dwords + 1u;
    }
    return 0;
}

int gpu_core_execute_next_scheduled(uint64_t *out_fence)
{
    gpu_scheduler_job job;
    const gpu_firmware_blob *sqe;
    gpu_cp_bind_config cp_config;
    gpu_cp_snapshot cp = {0};
    uint32_t gx_ack = 0u;
    uint32_t completion_magic;
    uint32_t completion = 0u;
    uint8_t submitted = 0u;
    int rc;

    if (out_fence)
        *out_fence = 0u;
    if (!out_fence || !g_runtime_backend_ready ||
        g_mesart_3d_inflight.active ||
        !g_gpu_regs_window.cpu_mapped || !g_gpu_gmu_window.cpu_mapped ||
        !g_runtime_ring.buffer.iova || !g_cp_pwrup.iova || !g_cp_shadow.iova ||
        !g_cp_completion.cpu || !g_cp_completion.iova)
        return -1;
    if (gpu_scheduler_peek_next(&g_scheduler, &job) != 0)
        return -2;
    if (gpu_core_validate_mesart_ring(job.ring, job.writable_gpu_va,
                                      job.writable_gpu_bytes,
                                      job.policy_flags) != 0)
        return -3;
    sqe = gpu_firmware_find(&g_firmware, GPU_FIRMWARE_SQE);
    if (!sqe || !gpu_firmware_payload_iova(sqe))
        return -4;
    if (gpu_ring_reset(&g_runtime_ring) != 0)
        return -5;
    completion_magic = GPU_CORE_RUNTIME_COMPLETION_MAGIC ^ (uint32_t)job.fence;
    if (!completion_magic)
        completion_magic = GPU_CORE_RUNTIME_COMPLETION_MAGIC;
    *(uint32_t *)g_cp_completion.cpu = 0u;
    gpu_buffer_prepare_for_device(&g_cp_completion);

    /* The boot-time probe supplies a known-good CP state and GX lease.  Keep
     * that lease across trusted runtime jobs: immediately cycling it faults
     * some X1E firmware revisions.  CP_ME_INIT still reapplies the private
     * pwrup record before a sealed Mesart batch is appended. */
    if (!g_runtime_gx_lease_held)
    {
        rc = gpu_gmu_gen7_acquire_gpu(&g_gpu_gmu_window, &gx_ack);
        if (rc != 0)
            return -6;
        g_runtime_gx_lease_held = 1u;
    }
    if (gpu_cp_status_read(&g_gpu_regs_window, adreno_x1_85_cp_layout(),
                             &cp) != 0 || cp.hw_fault || cp.protect_status)
    {
        rc = -7;
        goto out;
    }
    if (adreno_x1_85_emit_minimal_cp_init(&g_runtime_ring,
                                          g_cp_pwrup.iova) != 0 ||
        gpu_ring_emit_many(&g_runtime_ring,
                           (const uint32_t *)job.ring->buffer.cpu,
                           job.ring->write_dwords) != 0 ||
        /* This write is kernel-generated and targets only the pre-mapped,
         * private completion page; it turns a consumed CP ring into a
         * meaningful fence independent of the allowed resource write. */
        adreno_x1_85_emit_cp_memory_probe(&g_runtime_ring,
                                           g_cp_completion.iova,
                                           completion_magic) != 0 ||
        gpu_ring_seal(&g_runtime_ring) != 0)
    {
        rc = -8;
        goto out;
    }
    cp_config = (gpu_cp_bind_config){
        gpu_firmware_payload_iova(sqe),
        g_runtime_ring.buffer.iova,
        g_cp_shadow.iova + GPU_CORE_CP_BR_RPTR_OFFSET,
        g_cp_shadow.iova + GPU_CORE_CP_BV_RPTR_OFFSET,
        (uint32_t)g_runtime_ring.buffer.size_bytes,
        ADRENO_X1_85_CP_RB_CNTL_BOOT,
        0u,
        ADRENO_X1_85_CP_BR_APRIV_MASK,
        ADRENO_X1_85_CP_AUX_APRIV_MASK,
        ADRENO_X1_85_CP_AUX_APRIV_MASK,
    };
    if (gpu_cp_bind(&g_gpu_regs_window, adreno_x1_85_cp_layout(),
                    &cp_config) != 0)
    {
        rc = -9;
        goto out;
    }
    g_runtime_ring_bound = 1u;
    rc = gpu_cp_submit_fenced(&g_gpu_regs_window, &g_gpu_gmu_window,
                              ADRENO_X1_85_GMU_AHB_FENCE_STATUS,
                              adreno_x1_85_cp_layout(), &g_runtime_ring);
    if (rc != 0)
    {
        rc = -10;
        goto out;
    }
    submitted = 1u;
    if (gpu_cp_wait_ring(&g_gpu_regs_window, adreno_x1_85_cp_layout(),
                         g_runtime_ring.write_dwords, 100000u, &cp) != 0 ||
        cp.hw_fault || cp.protect_status)
    {
        rc = -11;
        goto out;
    }
    /* CP's read pointer proves it consumed the final packet, but its RAM
     * write reaches CPU-observable memory asynchronously.  A single read
     * here made the otherwise healthy queued self-test intermittently report
     * -12 on a fresh boot.  Use the same bounded invalidate/poll discipline
     * as the RB fence in the 3D path. */
    for (uint32_t poll = 0u; poll < 100000u; ++poll)
    {
        asm_dma_invalidate_range(g_cp_completion.cpu, sizeof(completion));
        completion = *(uint32_t *)g_cp_completion.cpu;
        if (completion == completion_magic)
            break;
        asm_relax();
    }
    asm_dma_invalidate_range(g_cp_completion.cpu, sizeof(completion));
    completion = *(uint32_t *)g_cp_completion.cpu;
    if (completion != completion_magic)
    {
        rc = -12;
        goto out;
    }
    if (gpu_scheduler_complete(&g_scheduler, job.fence) != 0)
    {
        rc = -13;
        goto out;
    }
    *out_fence = job.fence;
    rc = 0;

out:
    /* Once CP binding or write-pointer publication has failed, the state of
     * the live GX command front-end is uncertain.  Do not retry or reuse the
     * runtime ring in this boot; a reboot restores the known-good baseline. */
    if (rc != 0 && (submitted || g_runtime_gx_lease_held))
    {
        (void)gpu_gmu_gen7_release_gpu(&g_gpu_gmu_window);
        g_runtime_gx_lease_held = 0u;
        g_runtime_ring_bound = 0u;
        g_runtime_backend_ready = 0u;
    }
    return rc;
}
