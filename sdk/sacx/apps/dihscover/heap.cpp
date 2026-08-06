#include "browser.h"
#include "tlsf.h"

#define BROWSER_HEAP_BYTES (16u * 1024u * 1024u)

static const sacx_api *g_heap_api;
static void *g_heap;
static tlsf_t g_tlsf;

void browser_heap_set_api(const sacx_api *api)
{
    g_heap_api = api;
}

int browser_heap_init(void)
{
    if (!g_tlsf && g_heap_api && SACX_API_HAS(g_heap_api, mem_alloc) &&
        g_heap_api->mem_alloc(BROWSER_HEAP_BYTES, &g_heap) == 0)
        g_tlsf = tlsf_create_with_pool(g_heap, BROWSER_HEAP_BYTES);
    return g_tlsf ? 0 : -1;
}

void browser_heap_reset(void)
{
    if (g_heap)
        g_tlsf = tlsf_create_with_pool(g_heap, BROWSER_HEAP_BYTES);
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
