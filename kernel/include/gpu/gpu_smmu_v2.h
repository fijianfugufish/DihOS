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

/* Read-only identification.  No SMMU or GPU state changes. */
int gpu_smmuv2_probe(const gpu_mmio_window *window, gpu_smmuv2_caps *out);

/* Reads the selected context-bank's latched fault registers; it does not
 * acknowledge, clear, or otherwise modify them. */
int gpu_smmuv2_read_context_fault(const gpu_mmio_window *window,
                                  const gpu_smmuv2_caps *caps,
                                  uint32_t context_bank,
                                  gpu_smmuv2_context_fault *out);

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
