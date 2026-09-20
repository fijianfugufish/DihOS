#include "gpu/gpu_cp.h"
#include "asm/asm.h"

static int write64(const gpu_mmio_window *regs, uint32_t offset, uint64_t value)
{
    if (gpu_mmio_try_write32(regs, offset, (uint32_t)value) != 0 ||
        gpu_mmio_try_write32(regs, offset + 4u, (uint32_t)(value >> 32)) != 0)
        return -1;
    return 0;
}

static int read64(const gpu_mmio_window *regs, uint32_t offset, uint64_t *out)
{
    uint32_t lo;
    uint32_t hi;

    if (!out || gpu_mmio_try_read32(regs, offset, &lo) != 0 ||
        gpu_mmio_try_read32(regs, offset + 4u, &hi) != 0)
        return -1;
    *out = (uint64_t)lo | ((uint64_t)hi << 32);
    return 0;
}

/* Runtime status deliberately excludes configuration/privilege registers.
 * The X1E log identifies CP_APRIV_CNTL as the last requested access before
 * a fault; observing that optional register must not gate a draw. */
int gpu_cp_status_read(const gpu_mmio_window *regs,
                       const gpu_cp_register_layout *layout,
                       gpu_cp_snapshot *out)
{
    gpu_cp_snapshot snapshot = {0};
    if (!regs || !layout || !out)
        return -1;
    if (gpu_mmio_try_read32(regs, layout->rb_rptr, &snapshot.rb_rptr) != 0 ||
        gpu_mmio_try_read32(regs, layout->rb_wptr, &snapshot.rb_wptr) != 0 ||
        gpu_mmio_try_read32(regs, layout->hw_fault, &snapshot.hw_fault) != 0 ||
        gpu_mmio_try_read32(regs, layout->protect_status,
                            &snapshot.protect_status) != 0)
        return -2;
    *out = snapshot;
    return 0;
}

int gpu_cp_snapshot_read(const gpu_mmio_window *regs,
                         const gpu_cp_register_layout *layout,
                         gpu_cp_snapshot *out)
{
    gpu_cp_snapshot snapshot = {0};

    if (!regs || !layout || !out)
        return -1;
    if (read64(regs, layout->rb_base, &snapshot.rb_base) != 0 ||
        read64(regs, layout->sqe_instruction_base,
               &snapshot.sqe_instruction_base) != 0 ||
        read64(regs, layout->rb_rptr_address,
               &snapshot.rb_rptr_address) != 0 ||
        read64(regs, layout->bv_rb_rptr_address,
               &snapshot.bv_rb_rptr_address) != 0 ||
        gpu_mmio_try_read32(regs, layout->rb_control,
                            &snapshot.rb_control) != 0 ||
        gpu_mmio_try_read32(regs, layout->address_mode_control,
                            &snapshot.address_mode_control) != 0 ||
        gpu_mmio_try_read32(regs, layout->apriv_control,
                            &snapshot.apriv_control) != 0 ||
        gpu_mmio_try_read32(regs, layout->bv_apriv_control,
                            &snapshot.bv_apriv_control) != 0 ||
        gpu_mmio_try_read32(regs, layout->lpac_apriv_control,
                            &snapshot.lpac_apriv_control) != 0 ||
        gpu_mmio_try_read32(regs, layout->rb_rptr, &snapshot.rb_rptr) != 0 ||
        gpu_mmio_try_read32(regs, layout->rb_wptr, &snapshot.rb_wptr) != 0 ||
        gpu_mmio_try_read32(regs, layout->sqe_control,
                            &snapshot.sqe_control) != 0 ||
        gpu_mmio_try_read32(regs, layout->hw_fault,
                            &snapshot.hw_fault) != 0 ||
        gpu_mmio_try_read32(regs, layout->protect_status,
                            &snapshot.protect_status) != 0)
        return -2;
    *out = snapshot;
    return 0;
}

int gpu_cp_bind(const gpu_mmio_window *regs,
                const gpu_cp_register_layout *layout,
                const gpu_cp_bind_config *config)
{
    if (!regs || !layout || !config || !config->sqe_iova ||
        !config->ring_iova || !config->rptr_iova || !config->bv_rptr_iova ||
        !config->ring_bytes ||
        (config->ring_bytes & (config->ring_bytes - 1u)))
        return -1;
    if ((layout->address_mode_control && config->address_mode_control &&
         gpu_mmio_try_write32(regs, layout->address_mode_control,
                              config->address_mode_control) != 0) ||
        (layout->apriv_control && config->apriv_control &&
         gpu_mmio_try_write32(regs, layout->apriv_control,
                              config->apriv_control) != 0) ||
        (layout->bv_apriv_control && config->bv_apriv_control &&
         gpu_mmio_try_write32(regs, layout->bv_apriv_control,
                              config->bv_apriv_control) != 0) ||
        (layout->lpac_apriv_control && config->lpac_apriv_control &&
         gpu_mmio_try_write32(regs, layout->lpac_apriv_control,
                              config->lpac_apriv_control) != 0) ||
        write64(regs, layout->rb_rptr_address, config->rptr_iova) != 0 ||
        write64(regs, layout->bv_rb_rptr_address, config->bv_rptr_iova) != 0 ||
        write64(regs, layout->sqe_instruction_base, config->sqe_iova) != 0 ||
        write64(regs, layout->rb_base, config->ring_iova) != 0 ||
        gpu_mmio_try_write32(regs, layout->rb_control,
                             config->ring_control) != 0 ||
        gpu_mmio_try_write32(regs, layout->sqe_control, 1u) != 0)
        return -2;
    return 0;
}

int gpu_cp_submit(const gpu_mmio_window *regs,
                  const gpu_cp_register_layout *layout,
                  const gpu_command_ring *ring)
{
    if (!regs || !layout || !ring || !ring->sealed || !ring->write_dwords ||
        ring->write_dwords > ring->capacity_dwords)
        return -1;
    return gpu_mmio_try_write32(regs, layout->rb_wptr, ring->write_dwords) == 0
        ? 0 : -2;
}

int gpu_cp_submit_fenced(const gpu_mmio_window *regs,
                         const gpu_mmio_window *gmu,
                         uint32_t gmu_fence_status_offset,
                         const gpu_cp_register_layout *layout,
                         const gpu_command_ring *ring)
{
    uint32_t status;

    if (!regs || !gmu || !layout || !ring || !ring->sealed ||
        !ring->write_dwords || ring->write_dwords > ring->capacity_dwords)
        return -1;
    /* The GMU reports a dropped CP_RB_WPTR write in bit 0.  Follow the A7xx
     * fenced-write rule: post the write, observe the GMU fence, and retry a
     * dropped update once before declaring the ring unavailable. */
    for (uint32_t attempt = 0u; attempt < 2u; ++attempt)
    {
        if (gpu_mmio_try_write32(regs, layout->rb_wptr,
                                 ring->write_dwords) != 0)
            return -2;
        asm_mmio_barrier();
        for (uint32_t poll = 0u; poll < 1000u; ++poll)
        {
            if (gpu_mmio_try_read32(gmu, gmu_fence_status_offset,
                                    &status) != 0)
                return -3;
            if ((status & 1u) == 0u)
                return 0;
            asm_relax();
        }
    }
    return -4;
}

int gpu_cp_wait_ring(const gpu_mmio_window *regs,
                     const gpu_cp_register_layout *layout,
                     uint32_t expected_rptr, uint32_t polls,
                     gpu_cp_snapshot *last)
{
    gpu_cp_snapshot snapshot = {0};

    if (!regs || !layout || !polls)
        return -1;
    for (uint32_t i = 0u; i < polls; ++i)
    {
        /* Completion only depends on CP_RB_RPTR.  A full CP snapshot also
         * reads optional/diagnostic Gen7 registers, and this X1-85 firmware
         * can data-abort those reads after a submitted job despite the live
         * CP front-end being healthy.  Do not turn an observation-only
         * register into a runtime completion dependency.  Callers take a
         * complete snapshot before binding; their own completion memory
         * fence remains the second post-submit integrity check. */
        if (gpu_mmio_try_read32(regs, layout->rb_rptr,
                                &snapshot.rb_rptr) != 0)
            return -2;
        if (snapshot.rb_rptr == expected_rptr)
        {
            if (last)
                *last = snapshot;
            return 0;
        }
    }
    if (last)
        *last = snapshot;
    return -3;
}
