#pragma once

#include <stdint.h>
#include "gpu/gpu_mmio.h"
#include "gpu/gpu_ring.h"

/*
 * Family-neutral command-processor observation layer.  A GPU family supplies
 * the byte offsets of its CP registers; this layer never invents a packet or
 * changes hardware state.  Keeping the live-register check separate from the
 * Adreno implementation lets later GPU drivers reuse the same bring-up gate.
 */
typedef struct gpu_cp_register_layout
{
    uint32_t rb_base;
    uint32_t rb_control;
    uint32_t rb_rptr_address;
    uint32_t bv_rb_rptr_address;
    uint32_t address_mode_control;
    uint32_t apriv_control;
    uint32_t bv_apriv_control;
    uint32_t lpac_apriv_control;
    uint32_t rb_rptr;
    uint32_t rb_wptr;
    uint32_t sqe_control;
    uint32_t sqe_instruction_base;
    uint32_t hw_fault;
    uint32_t protect_status;
} gpu_cp_register_layout;

typedef struct gpu_cp_snapshot
{
    uint64_t rb_base;
    uint64_t sqe_instruction_base;
    uint64_t rb_rptr_address;
    uint64_t bv_rb_rptr_address;
    uint32_t rb_control;
    uint32_t address_mode_control;
    uint32_t apriv_control;
    uint32_t bv_apriv_control;
    uint32_t lpac_apriv_control;
    uint32_t rb_rptr;
    uint32_t rb_wptr;
    uint32_t sqe_control;
    uint32_t hw_fault;
    uint32_t protect_status;
} gpu_cp_snapshot;

typedef struct gpu_cp_bind_config
{
    uint64_t sqe_iova;
    uint64_t ring_iova;
    uint64_t rptr_iova;
    uint64_t bv_rptr_iova;
    uint32_t ring_bytes;
    uint32_t ring_control;
    uint32_t address_mode_control;
    uint32_t apriv_control;
    uint32_t bv_apriv_control;
    uint32_t lpac_apriv_control;
} gpu_cp_bind_config;

/* Reads the CP state while the caller holds the platform's GPU power lease.
 * The operation is intentionally read-only: it is the gate before firmware
 * binding, secure-world authentication, and later command submission. */
/* Runtime health only; configuration fields in the result remain zero. */
int gpu_cp_status_read(const gpu_mmio_window *regs,
                       const gpu_cp_register_layout *layout,
                       gpu_cp_snapshot *out);
int gpu_cp_snapshot_read(const gpu_mmio_window *regs,
                         const gpu_cp_register_layout *layout,
                         gpu_cp_snapshot *out);

/* Binds device-visible SQE firmware and a sealed command ring, then enables
 * the command front-end.  It does not advance the write pointer. */
int gpu_cp_bind(const gpu_mmio_window *regs,
                const gpu_cp_register_layout *layout,
                const gpu_cp_bind_config *config);

/* Publishes the sealed ring's current dword count as the hardware write
 * pointer.  The caller owns the device power lease for the entire sequence. */
int gpu_cp_submit(const gpu_mmio_window *regs,
                  const gpu_cp_register_layout *layout,
                  const gpu_command_ring *ring);

/* Publishes a Gen7 command-ring write pointer through the GMU AHB fence.
 * A failed fence means the GX power domain dropped the host write; callers
 * must not interpret a stale CP read pointer as a command decode failure. */
int gpu_cp_submit_fenced(const gpu_mmio_window *regs,
                         const gpu_mmio_window *gmu,
                         uint32_t gmu_fence_status_offset,
                         const gpu_cp_register_layout *layout,
                         const gpu_command_ring *ring);

/* Bounded completion check for a ring submitted at the current write point. */
int gpu_cp_wait_ring(const gpu_mmio_window *regs,
                     const gpu_cp_register_layout *layout,
                     uint32_t expected_rptr, uint32_t polls,
                     gpu_cp_snapshot *last);
