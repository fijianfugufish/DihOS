#include "gpu/gpu_gmu_image.h"

#define GMU_BLOCK_HEADER_BYTES 16u
#define GMU_ITCM_BASE          0x00000000u
#define GMU_TCM_BYTES          0x00004000u
#define GMU_GEN7_DTCM_BASE     0x10004000u

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int in_range(uint32_t address, uint32_t size, uint32_t base,
                    uint32_t length)
{
    uint64_t end = (uint64_t)address + size;
    return address >= base && end <= (uint64_t)base + length;
}

int gpu_gmu_image_parse(const gpu_firmware_set *firmware, gpu_gmu_image *out)
{
    const gpu_firmware_blob *gmu = 0;
    const uint8_t *bytes;
    uint32_t offset = 0u;

    if (!firmware || !out)
        return -1;
    *out = (gpu_gmu_image){0};
    for (uint32_t i = 0u; i < firmware->blob_count; ++i)
        if (firmware->blobs[i].role == GPU_FIRMWARE_GMU)
        {
            gmu = &firmware->blobs[i];
            break;
        }
    if (!gmu || !gmu->buffer.cpu || !gmu->buffer.size_bytes ||
        gmu->buffer.size_bytes > 0xffffffffull)
        return -2;
    bytes = (const uint8_t *)gmu->buffer.cpu;
    while (offset < (uint32_t)gmu->buffer.size_bytes)
    {
        uint32_t address;
        uint32_t size;
        gpu_gmu_segment *segment;

        if ((uint32_t)gmu->buffer.size_bytes - offset < GMU_BLOCK_HEADER_BYTES)
            return -3;
        address = read_le32(bytes + offset);
        size = read_le32(bytes + offset + 4u);
        offset += GMU_BLOCK_HEADER_BYTES;
        if (!size)
            continue;
        if (size > (uint32_t)gmu->buffer.size_bytes - offset ||
            out->segment_count == GPU_GMU_MAX_SEGMENTS)
            return -4;
        segment = &out->segments[out->segment_count++];
        segment->address = address;
        segment->size_bytes = size;
        segment->data = bytes + offset;
        if (in_range(address, size, GMU_ITCM_BASE, GMU_TCM_BYTES))
        {
            segment->target = GPU_GMU_SEGMENT_ITCM;
            out->itcm_bytes += size;
        }
        else if (in_range(address, size, GMU_GEN7_DTCM_BASE, GMU_TCM_BYTES))
        {
            segment->target = GPU_GMU_SEGMENT_DTCM;
            out->dtcm_bytes += size;
        }
        else
        {
            segment->target = GPU_GMU_SEGMENT_EXTERNAL;
            out->external_bytes += size;
        }
        offset += size;
    }
    return out->segment_count ? 0 : -5;
}
