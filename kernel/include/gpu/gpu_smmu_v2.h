#pragma once

#include <stdint.h>
#include "gpu/gpu_iommu.h"
#include "gpu/gpu_mmio.h"

/* The small architecture-specific part of the otherwise generic GPU DMA
 * domain.  It supports a non-secure ARM SMMUv2 in stream-matching mode and
 * attaches only fully prepared, stage-1 LPAE domains. */
typedef struct gpu_smmuv2_caps
{
    uint32_t id0;
    uint32_t id1;
    uint32_t id2;
    uint32_t page_shift;
    uint32_t page_count;
    uint32_t context_bank_count;
    uint32_t stream_group_count;
} gpu_smmuv2_caps;

/* Sticky, read-only fault state for one context bank.  It is captured after a
 * guarded bring-up failure so a GMU translation fault remains diagnosable
 * without enabling an interrupt path. */
typedef struct gpu_smmuv2_context_fault
{
    uint32_t fsr;
    uint32_t fsynr0;
    uint32_t fsynr1;
    uint64_t fault_address;
} gpu_smmuv2_context_fault;

/* Read-only configuration snapshot.  This makes the live stream-to-context
 * setup observable after a guarded bring-up without touching the SMMU. */
typedef struct gpu_smmuv2_context_state
{
    uint32_t sctlr;
    uint32_t tcr2;
    uint32_t tcr;
    uint32_t mair0;
    uint64_t ttbr0;
} gpu_smmuv2_context_state;

/* Global faults are where an unmatched Stream ID is reported; unlike a
 * context-bank fault, no selected CB necessarily owns such a request. */
typedef struct gpu_smmuv2_global_fault
{
    uint32_t gfsr;
    uint32_t gfsynr0;
    uint32_t gfsynr1;
    uint32_t gfsynr2;
} gpu_smmuv2_global_fault;

typedef struct gpu_smmuv2_stream_binding
{
    uint32_t stream_id;
    uint32_t smr_index;
    uint32_t smr;
    uint32_t s2cr;
} gpu_smmuv2_stream_binding;

/* Read-only identification.  No SMMU or GPU state changes. */
int gpu_smmuv2_probe(const gpu_mmio_window *window, gpu_smmuv2_caps *out);

/* Reads the selected context-bank's latched fault registers; it does not
 * acknowledge, clear, or otherwise modify them. */
int gpu_smmuv2_read_context_fault(const gpu_mmio_window *window,
                                  const gpu_smmuv2_caps *caps,
                                  uint32_t context_bank,
                                  gpu_smmuv2_context_fault *out);

/* Read-only SMMU state snapshots used for guarded GPU diagnostics. */
int gpu_smmuv2_read_context_state(const gpu_mmio_window *window,
                                  const gpu_smmuv2_caps *caps,
                                  uint32_t context_bank,
                                  gpu_smmuv2_context_state *out);
int gpu_smmuv2_read_global_fault(const gpu_mmio_window *window,
                                 gpu_smmuv2_global_fault *out);
int gpu_smmuv2_find_stream_binding(const gpu_mmio_window *window,
                                   const gpu_smmuv2_caps *caps,
                                   uint32_t stream_id,
                                   gpu_smmuv2_stream_binding *out);

/* Programs the requested context bank and unused stream-match registers,
 * then exposes the plan's streams only after the page-table root is
 * installed.  Qualcomm assigns CB0 to render traffic; companion engines
 * such as the GMU must use their separately assigned bank.  The caller
 * retains ownership of the supplied page tables for as long as the
 * attachment is live. */
int gpu_smmuv2_attach_context(const gpu_mmio_window *window,
                              const gpu_smmuv2_caps *caps,
                              const gpu_iommu_attach_plan *plan,
                              uint32_t context_bank,
                              uint32_t *out_bound_stream_count);

/* Convenience wrapper for the Adreno render context, which is architecturally
 * assigned to CB0. */
int gpu_smmuv2_attach(const gpu_mmio_window *window,
                      const gpu_smmuv2_caps *caps,
                      const gpu_iommu_attach_plan *plan,
                      uint32_t *out_context_bank,
                      uint32_t *out_bound_stream_count);
