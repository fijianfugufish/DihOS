#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "kwrappers/kimg.h"

void *pmem_alloc_pages(uint64_t pages)
{
    size_t bytes = (size_t)pages * 4096u;
    void *ptr = malloc(bytes);
    if (ptr) memset(ptr, 0, bytes);
    return ptr;
}
void pmem_free_pages(void *ptr, uint64_t pages) { (void)pages; free(ptr); }

int main(void)
{
    static const char svg[] =
        "<svg width='32' height='24' viewBox='0 0 32 24'>"
        "<rect x='1' y='1' width='10' height='8' fill='#ff0000'/>"
        "<circle cx='20' cy='12' r='5' fill='#00ff00'/>"
        "<path d='M 2 20 L 12 20 L 7 12 Z' fill='#0000ff'/>"
        "</svg>";
    kimg image;
    uint32_t nonzero = 0u;
    if (kimg_load_svg_memory(&image, svg, (uint32_t)(sizeof(svg) - 1u)) != 0)
        return 1;
    if (image.w != 32u || image.h != 24u || !image.px)
        return 2;
    for (uint32_t i = 0u; i < image.w * image.h; ++i)
        if (image.px[i]) ++nonzero;
    pmem_free_pages(image.px, ((uint64_t)image.w * image.h * 4u + 4095u) >> 12);
    if (nonzero < 100u)
        return 3;
    puts("SVG memory decoder tests passed");
    return 0;
}
