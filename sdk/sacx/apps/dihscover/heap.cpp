#include "dihscover.h"
#include "tlsf.h"

#define BROWSER_HEAP_BYTES (64u * 1024u * 1024u)

static const sacx_api *g_heap_api;
static void *g_heap;
static uint32_t g_heap_bytes;
static tlsf_t g_tlsf;

void browser_heap_set_api(const sacx_api *api)
{
    g_heap_api = api;
}

int browser_heap_init(void)
{
    static const uint32_t heap_options[] = {
        BROWSER_HEAP_BYTES,
        48u * 1024u * 1024u,
        32u * 1024u * 1024u,
        24u * 1024u * 1024u,
        16u * 1024u * 1024u
    };
    if (!g_tlsf && g_heap_api && SACX_API_HAS(g_heap_api, mem_alloc))
    {
        for (uint32_t i = 0u; i < (uint32_t)(sizeof(heap_options) / sizeof(heap_options[0])); ++i)
        {
            void *heap = 0;
            if (g_heap_api->mem_alloc(heap_options[i], &heap) != 0 || !heap)
                continue;
            g_tlsf = tlsf_create_with_pool(heap, heap_options[i]);
            if (g_tlsf)
            {
                g_heap = heap;
                g_heap_bytes = heap_options[i];
                break;
            }
            if (SACX_API_HAS(g_heap_api, mem_free))
                (void)g_heap_api->mem_free(heap);
        }
    }
    return g_tlsf ? 0 : -1;
}

void browser_heap_reset(void)
{
    if (g_heap && g_heap_bytes)
        g_tlsf = tlsf_create_with_pool(g_heap, g_heap_bytes);
}

void *browser_heap_alloc(uint32_t size)
{
    return g_tlsf && size ? tlsf_malloc(g_tlsf, size) : 0;
}

void *browser_heap_realloc(void *ptr, uint32_t size)
{
    return g_tlsf ? tlsf_realloc(g_tlsf, ptr, size) : 0;
}

void browser_heap_free(void *ptr)
{
    if (g_tlsf && ptr)
        tlsf_free(g_tlsf, ptr);
}

extern "C" void *malloc(__SIZE_TYPE__ size)
{
    return size <= 0xffffffffu ? browser_heap_alloc((uint32_t)size) : 0;
}

extern "C" void *calloc(__SIZE_TYPE__ count, __SIZE_TYPE__ size)
{
    if (count && size > 0xffffffffu / count) return 0;
    uint32_t bytes = (uint32_t)(count * size);
    void *ptr = browser_heap_alloc(bytes);
    if (ptr) dihs_memset(ptr, 0, bytes);
    return ptr;
}

extern "C" void *realloc(void *ptr, __SIZE_TYPE__ size)
{
    return size <= 0xffffffffu ? browser_heap_realloc(ptr, (uint32_t)size) : 0;
}

extern "C" void free(void *ptr)
{
    browser_heap_free(ptr);
}
