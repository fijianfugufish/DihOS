#include "gpu/gpu_gmu_boot.h"
#include "asm/asm.h"

/* Register offsets are direct byte offsets within the GMU resource.  The
 * Gen7 block uses the same CM3 TCM windows as the upstream A6xx/A7xx GMU
 * implementation: registers 0x0c00 and 0x1c00, expressed in dwords. */
#define GPU_GMU_CM3_ITCM_OFFSET  (0x00000c00u * sizeof(uint32_t))
#define GPU_GMU_CM3_DTCM_OFFSET  (0x00001c00u * sizeof(uint32_t))
#define GPU_GMU_TCM_BYTES        0x00004000u
#define GPU_GMU_GEN7_DTCM_BASE   0x10004000u
#define GPU_GMU_DTCM_BREADCRUMB_OFFSET \
    (GPU_GMU_CM3_DTCM_OFFSET + 0x00003fdcu)

/* A7xx registers, converted from their GMU dword addresses to byte offsets
 * within the 0x3d6a000 GMU resource. */
#define GPU_GMU_CM3_SYSRESET_OFFSET       (0x00005000u * sizeof(uint32_t))
#define GPU_GMU_CM3_BOOT_CONFIG_OFFSET    (0x00005001u * sizeof(uint32_t))
#define GPU_GMU_CM3_FW_INIT_RESULT_OFFSET (0x0000501cu * sizeof(uint32_t))
#define GPU_GMU_CM3_CFG_OFFSET            (0x0000502du * sizeof(uint32_t))
#define GPU_GMU_ICACHE_CONFIG_OFFSET       (0x00004c00u * sizeof(uint32_t))
#define GPU_GMU_DCACHE_CONFIG_OFFSET       (0x00004c01u * sizeof(uint32_t))
#define GPU_GMU_SYS_BUS_CONFIG_OFFSET      (0x00004c0fu * sizeof(uint32_t))
#define GPU_GMU_HFI_CTRL_STATUS_OFFSET    (0x00005180u * sizeof(uint32_t))
#define GPU_GMU_HFI_QTBL_INFO_OFFSET      (0x00005184u * sizeof(uint32_t))
#define GPU_GMU_HFI_QTBL_ADDR_OFFSET      (0x00005185u * sizeof(uint32_t))
#define GPU_GMU_HFI_CTRL_INIT_OFFSET      (0x00005186u * sizeof(uint32_t))
#define GPU_GMU_GENERAL_8_OFFSET          (0x000051cdu * sizeof(uint32_t))
#define GPU_GMU_GENERAL_9_OFFSET          (0x000051ceu * sizeof(uint32_t))
#define GPU_GMU_GENERAL_10_OFFSET         (0x000051cfu * sizeof(uint32_t))
#define GPU_GMU_AHB_FENCE_RANGE_0_OFFSET  (0x00009311u * sizeof(uint32_t))
#define GPU_GMU_GMU2HOST_INTR_CLR_OFFSET  (0x00005191u * sizeof(uint32_t))
#define GPU_GMU_GMU2HOST_INTR_INFO_OFFSET (0x00005192u * sizeof(uint32_t))
#define GPU_GMU_HOST2GMU_INTR_SET_OFFSET  (0x00005194u * sizeof(uint32_t))

/* Gen7 (non-legacy) GPU_SET uses HOST2GMU bit 30; acknowledgement and
 * release both use bit 31 in their respective interrupt registers. */
#define GPU_GMU_GEN7_GPU_SET_REQUEST (1u << 30)
#define GPU_GMU_GEN7_GPU_SET_ACK     (1u << 31)

#define GPU_GMU_A7XX_FENCE_RANGE_0 (0x80000000u | (0x32u << 18) | 0x8a0u)

int gpu_gmu_tcm_preflight(const gpu_mmio_window *gmu,
                          uint32_t *out_reset_signature)
{
    if (!out_reset_signature)
        return -1;
    return gpu_mmio_try_read32(gmu, GPU_GMU_CM3_DTCM_OFFSET +
                               GPU_GMU_TCM_BYTES - 8u * sizeof(uint32_t),
                               out_reset_signature);
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int upload_segment(const gpu_mmio_window *gmu,
                          const gpu_gmu_segment *segment,
                          uint32_t tcm_offset)
{
    uint32_t segment_offset;

    if (!gmu || !segment || !segment->data ||
        (segment->size_bytes & 3u) ||
        segment->size_bytes > GPU_GMU_TCM_BYTES)
        return -1;
    if (segment->target == GPU_GMU_SEGMENT_ITCM)
        segment_offset = segment->address;
    else if (segment->target == GPU_GMU_SEGMENT_DTCM &&
             segment->address >= GPU_GMU_GEN7_DTCM_BASE)
        segment_offset = segment->address - GPU_GMU_GEN7_DTCM_BASE;
    else
        return -2;
    if (segment_offset > GPU_GMU_TCM_BYTES ||
        segment->size_bytes > GPU_GMU_TCM_BYTES - segment_offset ||
        tcm_offset > gmu->size_bytes ||
        segment_offset > gmu->size_bytes - tcm_offset ||
        segment->size_bytes > gmu->size_bytes - tcm_offset - segment_offset)
        return -3;

    for (uint32_t byte = 0u; byte < segment->size_bytes; byte += 4u)
        if (gpu_mmio_try_write32(gmu, tcm_offset + segment_offset + byte,
                                 read_le32(segment->data + byte)) != 0)
            return -4;
    return 0;
}

int gpu_gmu_upload_tcm(const gpu_mmio_window *gmu,
                       const gpu_gmu_image *image,
                       uint32_t *out_segments_written,
                       uint32_t *out_bytes_written)
{
    uint32_t segments = 0u;
    uint32_t bytes = 0u;

    if (out_segments_written)
        *out_segments_written = 0u;
    if (out_bytes_written)
        *out_bytes_written = 0u;
    if (!gmu || !gmu->cpu_mapped || !image || !image->segment_count)
        return -1;

    for (uint32_t i = 0u; i < image->segment_count; ++i)
    {
        const gpu_gmu_segment *segment = &image->segments[i];
        uint32_t tcm_offset;

        if (segment->target == GPU_GMU_SEGMENT_EXTERNAL)
            continue;
        tcm_offset = segment->target == GPU_GMU_SEGMENT_ITCM ?
                         GPU_GMU_CM3_ITCM_OFFSET : GPU_GMU_CM3_DTCM_OFFSET;
        if (upload_segment(gmu, segment, tcm_offset) != 0)
            return -2;
        ++segments;
        bytes += segment->size_bytes;
    }
    if (!segments)
        return -3;
    if (out_segments_written)
        *out_segments_written = segments;
    if (out_bytes_written)
        *out_bytes_written = bytes;
    return 0;
}

static int gmu_write(const gpu_mmio_window *gmu, uint32_t offset,
                     uint32_t value)
{
    return gpu_mmio_try_write32(gmu, offset, value) == 0 ? 0 : -1;
}

static int gmu_poll_mask(const gpu_mmio_window *gmu, uint32_t offset,
                         uint32_t mask, uint32_t expected,
                         uint32_t *out_value)
{
    uint32_t value = 0u;

    for (uint32_t attempt = 0u; attempt < 100000u; ++attempt)
    {
        if (gpu_mmio_try_read32(gmu, offset, &value) != 0)
            return -1;
        if ((value & mask) == expected)
        {
            if (out_value)
                *out_value = value;
            return 0;
        }
        /* The upstream sequence polls at 100-us intervals for up to 10 ms.
         * A tight EL1 read loop expires orders of magnitude sooner on the
         * X Elite, so give a peripheral state transition actual time between
         * observations.  This is only reached after a failed read. */
        for (uint32_t spin = 0u; spin < 256u; ++spin)
            asm_relax();
    }
    if (out_value)
        *out_value = value;
    return -2;
}

static void gmu_capture_breadcrumb(const gpu_mmio_window *gmu,
                                   gpu_gmu_start_status *status)
{
    if (!gmu || !status)
        return;
    (void)gpu_mmio_try_read32(gmu, GPU_GMU_DTCM_BREADCRUMB_OFFSET,
                              &status->dtcm_breadcrumb);
}

int gpu_gmu_gen7_start_hfi_control(const gpu_mmio_window *gmu,
                                   gpu_gmu_start_status *in_out_status)
{
    gpu_gmu_start_status local = {0};
    gpu_gmu_start_status *status = in_out_status ? in_out_status : &local;

    if (!gmu || !gmu->cpu_mapped)
        return -1;
    for (uint32_t attempt = 1u; attempt <= 3u; ++attempt)
    {
        status->hfi_control_attempts = attempt;
        if (gmu_write(gmu, GPU_GMU_HFI_CTRL_INIT_OFFSET, 1u) != 0)
            return -2;
        if (gmu_poll_mask(gmu, GPU_GMU_HFI_CTRL_STATUS_OFFSET, 1u, 1u,
                          &status->hfi_control_status) == 0)
            return 0;
    }
    gmu_capture_breadcrumb(gmu, status);
    return -3;
}

int gpu_gmu_gen7_acquire_gpu(const gpu_mmio_window *gmu,
                             uint32_t *out_ack_status)
{
    uint32_t status = 0u;

    if (out_ack_status)
        *out_ack_status = 0u;
    if (!gmu || !gmu->cpu_mapped ||
        gmu_write(gmu, GPU_GMU_HOST2GMU_INTR_SET_OFFSET,
                  GPU_GMU_GEN7_GPU_SET_REQUEST) != 0)
        return -1;
    if (gmu_poll_mask(gmu, GPU_GMU_GMU2HOST_INTR_INFO_OFFSET,
                      GPU_GMU_GEN7_GPU_SET_ACK,
                      GPU_GMU_GEN7_GPU_SET_ACK, &status) != 0)
    {
        /* Do not leave a failed acquire request latched in the GMU. */
        (void)gmu_write(gmu, GPU_GMU_HOST2GMU_INTR_SET_OFFSET,
                        GPU_GMU_GEN7_GPU_SET_ACK);
        if (out_ack_status)
            *out_ack_status = status;
        return -2;
    }
    if (gmu_write(gmu, GPU_GMU_GMU2HOST_INTR_CLR_OFFSET,
                  GPU_GMU_GEN7_GPU_SET_ACK) != 0)
        return -3;
    if (out_ack_status)
        *out_ack_status = status;
    return 0;
}

int gpu_gmu_gen7_release_gpu(const gpu_mmio_window *gmu)
{
    if (!gmu || !gmu->cpu_mapped)
        return -1;
    return gmu_write(gmu, GPU_GMU_HOST2GMU_INTR_SET_OFFSET,
                     GPU_GMU_GEN7_GPU_SET_ACK);
}

int gpu_gmu_start_cold(const gpu_mmio_window *gmu,
                       const gpu_gmu_memory *memory,
                       uint32_t reset_signature, uint32_t chip_id,
                       gpu_gmu_start_status *out_status)
{
    gpu_gmu_start_status status = {0};
    uint32_t init_mask;
    uint32_t init_expected;
    uint32_t log_config;

    if (!gmu || !memory || !memory->hfi.cpu || !memory->log.cpu ||
        memory->hfi.iova != GPU_GMU_HFI_IOVA ||
        memory->log.iova != GPU_GMU_LOG_IOVA ||
        memory->hfi_queue_count != 2u ||
        memory->log.size_bytes < 4096u ||
        GPU_GMU_HFI_IOVA > 0xffffffffull ||
        GPU_GMU_LOG_IOVA > 0xffffffffull)
        return -1;
    /* Firmware generations expose two reset-result encodings.  The DTCM
     * probe chooses the same validation rule used by the upstream driver. */
    if (reset_signature <= 0x20010004u)
    {
        init_mask = 0xffffffffu;
        init_expected = 0xbabefaceu;
    }
    else
    {
        init_mask = 0x1ffu;
        init_expected = 0x100u;
    }
    log_config = (uint32_t)(GPU_GMU_LOG_IOVA & 0xfffff000ull) |
                 ((uint32_t)(memory->log.size_bytes / 4096u - 1u) & 0xffu);

    /* Gen7 uses these cache/bus settings before reset release.  They disable
     * GMU writeback/readback buffering; power-collapse tuning is deliberately
     * left to the firmware's A7xx defaults. */
    if (gmu_write(gmu, GPU_GMU_SYS_BUS_CONFIG_OFFSET, 1u) != 0 ||
        gmu_write(gmu, GPU_GMU_ICACHE_CONFIG_OFFSET, 1u) != 0 ||
        gmu_write(gmu, GPU_GMU_DCACHE_CONFIG_OFFSET, 1u) != 0 ||
        gmu_write(gmu, GPU_GMU_CM3_FW_INIT_RESULT_OFFSET, 0u) != 0 ||
        gmu_write(gmu, GPU_GMU_CM3_BOOT_CONFIG_OFFSET, 2u) != 0 ||
        gmu_write(gmu, GPU_GMU_HFI_QTBL_ADDR_OFFSET,
                  (uint32_t)GPU_GMU_HFI_IOVA) != 0 ||
        gmu_write(gmu, GPU_GMU_HFI_QTBL_INFO_OFFSET, 1u) != 0 ||
        gmu_write(gmu, GPU_GMU_AHB_FENCE_RANGE_0_OFFSET,
                  GPU_GMU_A7XX_FENCE_RANGE_0) != 0 ||
        gmu_write(gmu, GPU_GMU_CM3_CFG_OFFSET, 0x4052u) != 0 ||
        gmu_write(gmu, GPU_GMU_GENERAL_10_OFFSET, chip_id) != 0 ||
        gmu_write(gmu, GPU_GMU_GENERAL_8_OFFSET, log_config) != 0 ||
        gmu_write(gmu, GPU_GMU_CM3_SYSRESET_OFFSET, 1u) != 0 ||
        gmu_write(gmu, GPU_GMU_GENERAL_9_OFFSET, 0u) != 0)
        return -2;
    /* Our guarded MMIO helper performs one isolated store at a time.  Make
     * the complete boot configuration visible to the GMU before dropping
     * SYSRESET: Linux's writel path supplies the corresponding MMIO ordering
     * guarantee implicitly. */
    asm_mmio_barrier();
    if (gmu_write(gmu, GPU_GMU_CM3_SYSRESET_OFFSET, 0u) != 0)
        return -2;
    asm_mmio_barrier();
    if (gmu_poll_mask(gmu, GPU_GMU_CM3_FW_INIT_RESULT_OFFSET, init_mask,
                      init_expected, &status.fw_init_result) != 0)
    {
        /* A posted peripheral read can complete at the polling boundary.
         * Never reject a cold boot when one fresh, guarded read confirms the
         * exact firmware-ready value the loop was waiting for. */
        uint32_t confirmed = 0u;

        if ((status.fw_init_result & init_mask) == init_expected ||
            (gpu_mmio_try_read32(gmu, GPU_GMU_CM3_FW_INIT_RESULT_OFFSET,
                                 &confirmed) == 0 &&
             (confirmed & init_mask) == init_expected))
        {
            status.fw_init_result = confirmed;
            status.fw_init_confirmed_after_poll = 1u;
        }
        /* gen70500 reports 0x901 on this X1E firmware: bit 9 is already
         * outside the upstream ready mask and bit 0 is an additional
         * firmware status bit.  Do not claim GMU success from it, but allow
         * the non-destructive HFI-control handshake to determine whether the
         * running firmware accepts its prepared queue table. */
        else if (reset_signature > 0x20010004u &&
            (status.fw_init_result & 0x1ffu) == 0x101u)
            status.fw_init_used_compatibility_bit = 1u;
        else
        {
            gmu_capture_breadcrumb(gmu, &status);
            if (out_status)
                *out_status = status;
            return -3;
        }
    }
    /* The HFI controller is clocked by the just-started CM3 firmware.  Give
     * it the same bounded 10 ms window used by the upstream sequence, then
     * reassert the idempotent INIT latch twice before declaring it absent.
     * This covers the cold-boot race without resetting the GMU or touching
     * GX power. */
    if (gpu_gmu_gen7_start_hfi_control(gmu, &status) != 0)
    {
        gmu_capture_breadcrumb(gmu, &status);
        if (out_status)
            *out_status = status;
        return -4;
    }
    if (out_status)
        *out_status = status;
    return 0;
}
