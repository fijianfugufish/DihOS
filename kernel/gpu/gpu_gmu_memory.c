#include "gpu/gpu_gmu_memory.h"

#define GPU_GMU_HFI_QUEUE_COUNT 2u
#define GPU_GMU_HFI_QUEUE_BYTES 0x1000u
#define GPU_GMU_HFI_TABLE_WORDS 6u
#define GPU_GMU_HFI_QUEUE_HEADER_WORDS 12u

/* The Gen7 HFI queue-table ABI is deliberately expressed as dwords here to
 * keep its on-wire 24-byte table header and 48-byte queue headers independent
 * of compiler packing rules.  Queue 0 is H2F and queue 1 is F2H. */
static int gmu_hfi_init(gpu_gmu_memory *memory)
{
    uint32_t *table;

    if (!memory || !memory->hfi.cpu ||
        memory->hfi.size_bytes < 3u * GPU_GMU_HFI_QUEUE_BYTES ||
        GPU_GMU_HFI_IOVA > 0xffffffffull -
                               2u * GPU_GMU_HFI_QUEUE_BYTES)
        return -1;

    table = (uint32_t *)memory->hfi.cpu;
    /* table: version, byte-size, first-header offset (dwords), header size
     * (dwords), total queues and active queues. */
    table[0] = 0u;
    table[1] = (GPU_GMU_HFI_TABLE_WORDS +
                GPU_GMU_HFI_QUEUE_COUNT * GPU_GMU_HFI_QUEUE_HEADER_WORDS) *
               sizeof(uint32_t);
    table[2] = GPU_GMU_HFI_TABLE_WORDS;
    table[3] = GPU_GMU_HFI_QUEUE_HEADER_WORDS;
    table[4] = GPU_GMU_HFI_QUEUE_COUNT;
    table[5] = GPU_GMU_HFI_QUEUE_COUNT;

    for (uint32_t queue_id = 0u; queue_id < GPU_GMU_HFI_QUEUE_COUNT;
         ++queue_id)
    {
        uint32_t *header = table + GPU_GMU_HFI_TABLE_WORDS +
                           queue_id * GPU_GMU_HFI_QUEUE_HEADER_WORDS;
        uint32_t queue_iova = (uint32_t)(GPU_GMU_HFI_IOVA +
                                         (queue_id + 1u) *
                                             GPU_GMU_HFI_QUEUE_BYTES);

        header[0] = 1u;                         /* status */
        header[1] = queue_iova;                 /* queue IOVA */
        header[2] = (10u << 8) | queue_id;      /* HFI queue type */
        header[3] = GPU_GMU_HFI_QUEUE_BYTES / sizeof(uint32_t);
        header[4] = 0u;                         /* message size */
        header[5] = 0u;                         /* dropped */
        header[6] = 1u;                         /* RX watermark */
        header[7] = 1u;                         /* TX watermark */
        header[8] = 1u;                         /* RX request */
        header[9] = 0u;                         /* TX request */
        header[10] = 0u;                        /* read index */
        header[11] = 0u;                        /* write index */
    }
    memory->hfi_queue_count = GPU_GMU_HFI_QUEUE_COUNT;
    return 0;
}

static void copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t count)
{
    while (count--)
        *dst++ = *src++;
}

void gpu_gmu_memory_release(gpu_gmu_memory *memory)
{
    if (!memory)
        return;
    gpu_buffer_release(&memory->log);
    gpu_buffer_release(&memory->hfi);
    gpu_buffer_release(&memory->debug);
    gpu_buffer_release(&memory->dummy);
    gpu_buffer_release(&memory->icache);
    *memory = (gpu_gmu_memory){0};
}

int gpu_gmu_memory_stage(const gpu_gmu_image *image, gpu_gmu_memory *out)
{
    gpu_gmu_memory memory = {0};

    if (!image || !out || !image->segment_count)
        return -1;
    if (gpu_buffer_alloc(&memory.icache, GPU_GMU_ICACHE_BYTES,
                         GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0 ||
        gpu_buffer_alloc(&memory.dummy, GPU_GMU_DUMMY_BYTES,
                         GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0 ||
        gpu_buffer_alloc(&memory.debug, GPU_GMU_DEBUG_BYTES,
                         GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0 ||
        gpu_buffer_alloc(&memory.hfi, GPU_GMU_HFI_BYTES,
                         GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0 ||
        gpu_buffer_alloc(&memory.log, GPU_GMU_LOG_BYTES,
                         GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0)
        goto fail;

    for (uint32_t i = 0u; i < image->segment_count; ++i)
    {
        const gpu_gmu_segment *segment = &image->segments[i];
        uint64_t end = (uint64_t)segment->address + segment->size_bytes;

        if (segment->target != GPU_GMU_SEGMENT_EXTERNAL)
            continue;
        if (segment->address < GPU_GMU_ICACHE_IOVA ||
            end > GPU_GMU_ICACHE_IOVA + memory.icache.size_bytes)
            goto fail;
        copy_bytes((uint8_t *)memory.icache.cpu +
                       (segment->address - GPU_GMU_ICACHE_IOVA),
                   segment->data, segment->size_bytes);
        ++memory.external_segments_loaded;
    }
    if (!memory.external_segments_loaded)
        goto fail;
    if (gmu_hfi_init(&memory) != 0)
        goto fail;
    gpu_buffer_prepare_for_device(&memory.icache);
    gpu_buffer_prepare_for_device(&memory.dummy);
    gpu_buffer_prepare_for_device(&memory.debug);
    gpu_buffer_prepare_for_device(&memory.hfi);
    gpu_buffer_prepare_for_device(&memory.log);
    *out = memory;
    return 0;
fail:
    gpu_gmu_memory_release(&memory);
    return -2;
}

int gpu_gmu_memory_map(gpu_iommu_domain *domain, gpu_gmu_memory *memory)
{
    if (!domain || !memory || !memory->external_segments_loaded ||
        gpu_iommu_map_buffer(domain, &memory->icache, GPU_GMU_ICACHE_IOVA) != 0 ||
        gpu_iommu_map_buffer(domain, &memory->dummy, GPU_GMU_DUMMY_IOVA) != 0 ||
        gpu_iommu_map_buffer(domain, &memory->debug, GPU_GMU_DEBUG_IOVA) != 0 ||
        gpu_iommu_map_buffer_with_attributes(domain, &memory->hfi,
                                             GPU_GMU_HFI_IOVA, 0u) != 0 ||
        gpu_iommu_map_buffer_with_attributes(domain, &memory->log,
                                             GPU_GMU_LOG_IOVA, 0u) != 0)
        return -1;
    return 0;
}
