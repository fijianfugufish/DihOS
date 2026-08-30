#include "gpu/gpu_iommu.h"

#define GPU_IOMMU_ENTRIES_PER_TABLE 512u
#define GPU_IOMMU_ADDR_MASK          0x0000FFFFFFFFF000ull

/* ARM long-descriptor values shared by SMMUv3 stage-1 page tables. */
#define GPU_IOMMU_DESC_VALID         (1ull << 0)
#define GPU_IOMMU_DESC_TABLE_OR_PAGE (1ull << 1)
#define GPU_IOMMU_DESC_AP_UNPRIV     (1ull << 6)
#define GPU_IOMMU_DESC_AF            (1ull << 10)
#define GPU_IOMMU_DESC_SH_INNER      (3ull << 8)
#define GPU_IOMMU_DESC_NG            (1ull << 11)

static uint64_t align_up(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint32_t table_index(uint64_t iova, uint32_t level)
{
    return (uint32_t)((iova >> (39u - level * 9u)) & 0x1ffull);
}

static uint64_t table_descriptor(uint64_t phys)
{
    return (phys & GPU_IOMMU_ADDR_MASK) |
           GPU_IOMMU_DESC_VALID | GPU_IOMMU_DESC_TABLE_OR_PAGE;
}

static uint64_t page_descriptor(uint64_t phys, uint32_t attributes)
{
    uint64_t access = attributes & GPU_IOMMU_MAP_PRIVILEGED ? 0u :
                      GPU_IOMMU_DESC_AP_UNPRIV;

    /* AttrIndx 0 (MAIR0 is Normal WB), read/write, inner-shareable, accessed
     * and non-global.  The AP bit is deliberately selected per mapping:
     * Gen7 fetches external code as privileged, while its HFI aperture is
     * accessed as host-shared (unprivileged) memory. */
    return (phys & GPU_IOMMU_ADDR_MASK) |
           GPU_IOMMU_DESC_VALID | GPU_IOMMU_DESC_TABLE_OR_PAGE |
           access | GPU_IOMMU_DESC_AF |
           GPU_IOMMU_DESC_SH_INNER | GPU_IOMMU_DESC_NG;
}

static gpu_buffer *new_table(gpu_iommu_domain *domain)
{
    gpu_buffer *table;

    if (!domain || domain->table_count >= GPU_IOMMU_MAX_TABLES)
        return 0;
    table = &domain->tables[domain->table_count];
    if (gpu_buffer_alloc(table, GPU_IOMMU_PAGE_SIZE, GPU_BUFFER_ZEROED) != 0)
        return 0;
    ++domain->table_count;
    return table;
}

static gpu_buffer *find_table(const gpu_iommu_domain *domain, uint64_t phys)
{
    if (!domain)
        return 0;
    for (uint32_t i = 0u; i < domain->table_count; ++i)
        if (domain->tables[i].phys == (phys & GPU_IOMMU_ADDR_MASK))
            return (gpu_buffer *)&domain->tables[i];
    return 0;
}

static int map_page(gpu_iommu_domain *domain, uint64_t iova, uint64_t phys,
                    uint32_t attributes)
{
    gpu_buffer *table;

    table = find_table(domain, domain->root_phys);
    if (!table)
        return -1;

    for (uint32_t level = domain->root_level; level < 3u; ++level)
    {
        uint64_t *entries = (uint64_t *)table->cpu;
        uint64_t *entry = &entries[table_index(iova, level)];
        gpu_buffer *next;

        if (!(*entry & GPU_IOMMU_DESC_VALID))
        {
            next = new_table(domain);
            if (!next)
                return -2;
            *entry = table_descriptor(next->phys);
        }
        else
        {
            if (!(*entry & GPU_IOMMU_DESC_TABLE_OR_PAGE))
                return -3;
            next = find_table(domain, *entry);
            if (!next)
                return -4;
        }
        table = next;
    }

    ((uint64_t *)table->cpu)[table_index(iova, 3u)] =
        page_descriptor(phys, attributes);
    return 0;
}

int gpu_iommu_domain_init(gpu_iommu_domain *domain, uint32_t va_bits)
{
    gpu_buffer *root;

    if (!domain || va_bits < GPU_IOMMU_MIN_VA_BITS ||
        va_bits > GPU_IOMMU_MAX_VA_BITS)
        return -1;
    *domain = (gpu_iommu_domain){0};
    root = new_table(domain);
    if (!root)
        return -2;
    domain->va_bits = va_bits;
    /* A 39-bit domain starts at L1; wider domains use an L0 root. */
    domain->root_level = va_bits == GPU_IOMMU_MIN_VA_BITS ? 1u : 0u;
    domain->root_phys = root->phys;
    return 0;
}

void gpu_iommu_domain_release(gpu_iommu_domain *domain)
{
    if (!domain)
        return;
    for (uint32_t i = 0u; i < domain->table_count; ++i)
        gpu_buffer_release(&domain->tables[i]);
    *domain = (gpu_iommu_domain){0};
}

int gpu_iommu_map_buffer(gpu_iommu_domain *domain, gpu_buffer *buffer,
                         uint64_t iova)
{
    return gpu_iommu_map_buffer_with_attributes(domain, buffer, iova,
                                                GPU_IOMMU_MAP_PRIVILEGED);
}

int gpu_iommu_map_buffer_with_attributes(gpu_iommu_domain *domain,
                                         gpu_buffer *buffer, uint64_t iova,
                                         uint32_t attributes)
{
    uint64_t mapped;
    uint64_t bytes;

    if (!domain || !buffer || !buffer->cpu || !buffer->phys ||
        !buffer->size_bytes || (iova & (GPU_IOMMU_PAGE_SIZE - 1u)) ||
        (attributes & ~GPU_IOMMU_MAP_PRIVILEGED))
        return -1;
    bytes = align_up(buffer->size_bytes, GPU_IOMMU_PAGE_SIZE);
    if (iova + bytes < iova || (iova + bytes) > (1ull << domain->va_bits))
        return -2;

    for (mapped = 0u; mapped < bytes; mapped += GPU_IOMMU_PAGE_SIZE)
    {
        int rc = map_page(domain, iova + mapped, buffer->phys + mapped,
                          attributes);
        if (rc != 0)
            return rc;
    }
    buffer->iova = iova;
    ++domain->mapping_count;
    return 0;
}

void gpu_iommu_domain_prepare_for_device(const gpu_iommu_domain *domain)
{
    if (!domain)
        return;
    for (uint32_t i = 0u; i < domain->table_count; ++i)
        gpu_buffer_prepare_for_device(&domain->tables[i]);
}

int gpu_iommu_build_attach_plan(const gpu_iommu_topology *topology,
                                uint64_t endpoint_base,
                                uint32_t architecture,
                                const gpu_iommu_domain *domain,
                                gpu_iommu_attach_plan *out)
{
    const gpu_iommu_endpoint *endpoint = 0;

    if (!topology || !domain || !out || !domain->root_phys ||
        !domain->table_count || !domain->mapping_count ||
        domain->va_bits < GPU_IOMMU_MIN_VA_BITS ||
        domain->va_bits > GPU_IOMMU_MAX_VA_BITS)
        return -1;
    for (uint32_t i = 0u; i < topology->endpoint_count; ++i)
    {
        const gpu_iommu_endpoint *candidate = &topology->endpoints[i];
        if (candidate->base == endpoint_base &&
            candidate->architecture == architecture)
        {
            endpoint = candidate;
            break;
        }
    }
    if (!endpoint || !endpoint->stream_id_count ||
        endpoint->stream_id_count > GPU_IOMMU_MAX_STREAM_IDS)
        return -2;

    *out = (gpu_iommu_attach_plan){0};
    out->endpoint_base = endpoint->base;
    out->domain_root_phys = domain->root_phys;
    out->architecture = endpoint->architecture;
    out->va_bits = domain->va_bits;
    out->stream_id_count = endpoint->stream_id_count;
    for (uint32_t i = 0u; i < endpoint->stream_id_count; ++i)
        out->stream_ids[i] = endpoint->stream_ids[i];
    return 0;
}
