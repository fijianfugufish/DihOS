#include "system/kimage_clipboard.h"
#include "memory/pmem.h"
#include "kwrappers/string.h"

static kimg G_image;

static void kimage_free(kimg *image)
{
    uint64_t bytes = 0u;
    uint64_t pages = 0u;

    if (!image)
        return;
    if (image->px && image->w && image->h)
    {
        bytes = (uint64_t)image->w * (uint64_t)image->h * 4ull;
        pages = (bytes + 4095ull) >> 12;
        if (pages)
            pmem_free_pages(image->px, pages);
    }
    image->px = 0;
    image->w = 0u;
    image->h = 0u;
}

static int kimage_clone(kimg *out, const kimg *source)
{
    uint64_t bytes = 0u;
    uint64_t pages = 0u;
    uint32_t *pixels = 0;

    if (!out || !source || !source->px || !source->w || !source->h)
        return -1;

    bytes = (uint64_t)source->w * (uint64_t)source->h * 4ull;
    if (bytes > 256ull * 1024ull * 1024ull)
        return -1;
    pages = (bytes + 4095ull) >> 12;
    pixels = (uint32_t *)pmem_alloc_pages(pages);
    if (!pixels)
        return -1;

    memcpy(pixels, source->px, (size_t)bytes);
    out->px = pixels;
    out->w = source->w;
    out->h = source->h;
    return 0;
}

int kimage_clipboard_set(const kimg *image)
{
    kimg replacement = {0};

    if (!image)
    {
        kimage_clipboard_clear();
        return 0;
    }
    if (kimage_clone(&replacement, image) != 0)
        return -1;

    kimage_free(&G_image);
    G_image = replacement;
    return 0;
}

int kimage_clipboard_copy(kimg *out)
{
    if (!out)
        return -1;
    *out = (kimg){0};
    return kimage_clone(out, &G_image);
}

int kimage_clipboard_has_image(void)
{
    return G_image.px && G_image.w && G_image.h;
}

void kimage_clipboard_clear(void)
{
    kimage_free(&G_image);
}
