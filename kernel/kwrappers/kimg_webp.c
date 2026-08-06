#include <stddef.h>
#include <stdint.h>
#include "kwrappers/kimg.h"
#include "memory/pmem.h"

typedef struct kimg_webp_allocation
{
    uint64_t pages;
} kimg_webp_allocation;

static void *kimg_webp_alloc(void *user, size_t size)
{
    uint64_t bytes;
    uint64_t pages;
    kimg_webp_allocation *allocation;
    (void)user;
    if (!size || size > 64u * 1024u * 1024u)
        return 0;
    bytes = (uint64_t)size + sizeof(*allocation);
    pages = (bytes + 4095u) >> 12;
    allocation = (kimg_webp_allocation *)pmem_alloc_pages(pages);
    if (!allocation)
        return 0;
    allocation->pages = pages;
    return allocation + 1;
}

static void kimg_webp_free(void *user, void *ptr)
{
    kimg_webp_allocation *allocation;
    (void)user;
    if (!ptr)
        return;
    allocation = (kimg_webp_allocation *)ptr - 1;
    pmem_free_pages(allocation, allocation->pages);
}

void *malloc(size_t size) { return kimg_webp_alloc(0, size); }
void free(void *ptr) { kimg_webp_free(0, ptr); }

#define SIMPLEWEBP_DISABLE_STDIO
#define SIMPLEWEBP_IMPLEMENTATION
#include "simplewebp.h"

int kimg_load_webp_memory(kimg *out, const void *data, uint32_t size)
{
    simplewebp_allocator allocator = {kimg_webp_alloc, kimg_webp_free, 0};
    simplewebp *decoder = 0;
    size_t width = 0u;
    size_t height = 0u;
    uint64_t pixels;
    uint64_t bytes;
    uint64_t pages;
    uint32_t *dst;
    simplewebp_error error;

    if (!out || !data || size < 12u || size > 16u * 1024u * 1024u)
        return -1;
    kimg_zero(out);
    error = simplewebp_load_from_memory((void *)data, size, &allocator, &decoder);
    if (error != SIMPLEWEBP_NO_ERROR || !decoder)
        return -1;
    simplewebp_get_dimensions(decoder, &width, &height);
    if (!width || !height || width > 4096u || height > 4096u)
    {
        simplewebp_unload(decoder);
        return -1;
    }
    pixels = (uint64_t)width * (uint64_t)height;
    bytes = pixels * 4u;
    if (!pixels || bytes > 64u * 1024u * 1024u)
    {
        simplewebp_unload(decoder);
        return -1;
    }
    pages = (bytes + 4095u) >> 12;
    dst = (uint32_t *)pmem_alloc_pages(pages);
    if (!dst)
    {
        simplewebp_unload(decoder);
        return -1;
    }
    error = simplewebp_decode(decoder, dst, 0);
    simplewebp_unload(decoder);
    if (error != SIMPLEWEBP_NO_ERROR)
    {
        pmem_free_pages(dst, pages);
        return -1;
    }
    for (uint64_t i = 0u; i < pixels; ++i)
    {
        const uint8_t *rgba = (const uint8_t *)&dst[i];
        dst[i] = ((uint32_t)rgba[3] << 24) | ((uint32_t)rgba[0] << 16) |
                 ((uint32_t)rgba[1] << 8) | rgba[2];
    }
    out->w = (uint32_t)width;
    out->h = (uint32_t)height;
    out->px = dst;
    return 0;
}
