#pragma once

#include <stdint.h>
#include "gpu/gpu_memory.h"

/*
 * Architecture-neutral GPU DMA-domain contract.  The current implementation
 * builds 4 KiB, four-level ARM stage-1 translation tables.  It deliberately
 * does not program an SMMU register or enable a stream: the architecture
 * specific attach backend owns that final transition.
 */
#define GPU_IOMMU_PAGE_SIZE       4096ull
#define GPU_IOMMU_MAX_TABLES      64u
#define GPU_IOMMU_MIN_VA_BITS     39u
#define GPU_IOMMU_MAX_VA_BITS     48u
#define GPU_IOMMU_MAX_ENDPOINTS   4u
#define GPU_IOMMU_MAX_STREAM_IDS  32u

/* Page-table access classes.  GMU firmware code is fetched through a
 * privileged mapping; its host-shared HFI queue is an unprivileged mapping.
 * Keep this distinction in the generic domain layer so a later GPU backend
 * need not duplicate the ARM descriptor details. */
#define GPU_IOMMU_MAP_PRIVILEGED  (1u << 0)

/* IORT node types used to select the eventual attach backend. */
#define GPU_IOMMU_ARCH_SMMU_V1V2  3u
#define GPU_IOMMU_ARCH_SMMU_V3    4u

typedef struct gpu_iommu_endpoint
{
    uint64_t base;
    uint32_t architecture;
    uint32_t stream_ids[GPU_IOMMU_MAX_STREAM_IDS];
    uint32_t stream_id_count;
} gpu_iommu_endpoint;

typedef struct gpu_iommu_topology
{
    gpu_iommu_endpoint endpoints[GPU_IOMMU_MAX_ENDPOINTS];
    uint32_t endpoint_count;
} gpu_iommu_topology;

/* A complete, inert handoff from the generic DMA domain to a selected IOMMU
 * endpoint.  An architecture-specific backend must validate its hardware
 * capabilities before consuming this plan and performing any register I/O. */
typedef struct gpu_iommu_attach_plan
{
    uint64_t endpoint_base;
    uint64_t domain_root_phys;
    uint32_t architecture;
    uint32_t va_bits;
    uint32_t stream_ids[GPU_IOMMU_MAX_STREAM_IDS];
    uint32_t stream_id_count;
} gpu_iommu_attach_plan;

typedef struct gpu_iommu_domain
{
    gpu_buffer tables[GPU_IOMMU_MAX_TABLES];
    uint32_t table_count;
    uint32_t mapping_count;
    uint32_t va_bits;
    uint32_t root_level;
    uint64_t root_phys;
} gpu_iommu_domain;

/* Allocates an empty, CPU-owned page-table tree. No device can observe it. */
int gpu_iommu_domain_init(gpu_iommu_domain *domain, uint32_t va_bits);
void gpu_iommu_domain_release(gpu_iommu_domain *domain);

/* Adds one contiguous buffer mapping and records its IOVA. The caller must
 * later bind the completed domain to the relevant device stream IDs. */
int gpu_iommu_map_buffer(gpu_iommu_domain *domain, gpu_buffer *buffer,
                         uint64_t iova);

/* Adds a mapping with an explicit stage-1 access class.  Attributes outside
 * GPU_IOMMU_MAP_PRIVILEGED are rejected instead of silently changing the
 * descriptor encoding. */
int gpu_iommu_map_buffer_with_attributes(gpu_iommu_domain *domain,
                                         gpu_buffer *buffer, uint64_t iova,
                                         uint32_t attributes);

/* Cleans all page-table memory after mapping changes; still no hardware I/O. */
void gpu_iommu_domain_prepare_for_device(const gpu_iommu_domain *domain);

/* Copies an endpoint and a completed page-table root into a bounded plan.
 * This is data preparation only; it cannot access an IOMMU. */
int gpu_iommu_build_attach_plan(const gpu_iommu_topology *topology,
                                uint64_t endpoint_base,
                                uint32_t architecture,
                                const gpu_iommu_domain *domain,
                                gpu_iommu_attach_plan *out);
