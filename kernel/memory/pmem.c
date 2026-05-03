#include "memory/pmem.h"
#include "bootinfo.h"

#define EFI_BOOT_SERVICES_CODE 3
#define EFI_BOOT_SERVICES_DATA 4
#define EFI_CONVENTIONAL_MEMORY 7
#define PAGE_SIZE 4096ull

typedef struct
{
    uint64_t base, len; // bytes
} range_t;

static inline int is_reclaimable_type(uint32_t t)
{
    // After ExitBootServices, BootServicesCode/Data and Conventional memory are reclaimable.
    return (t == EFI_BOOT_SERVICES_CODE ||
            t == EFI_BOOT_SERVICES_DATA ||
            t == EFI_CONVENTIONAL_MEMORY);
}

static range_t pool[256];
static uint32_t pool_count = 0;
static range_t pool_exec[256];
static uint32_t pool_exec_count = 0;

// Minimal descriptor layout per UEFI spec (version-independent via desc_size stride)
typedef struct
{
    uint32_t Type;
    uint32_t Pad; // keep natural alignment
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} efi_mdesc;

static void range_add(uint64_t base, uint64_t len)
{
    if (!len)
        return;
    if (pool_count < (uint32_t)(sizeof(pool) / sizeof(pool[0])))
    {
        pool[pool_count++] = (range_t){base, len};
    }
}

static void range_add_exec(uint64_t base, uint64_t len)
{
    if (!len)
        return;
    if (pool_exec_count < (uint32_t)(sizeof(pool_exec) / sizeof(pool_exec[0])))
    {
        pool_exec[pool_exec_count++] = (range_t){base, len};
    }
}

static uint64_t align_up(uint64_t x, uint64_t a) { return (x + (a - 1)) & ~(a - 1); }
static uint64_t align_down(uint64_t x, uint64_t a) { return x & ~(a - 1); }

static void range_append(range_t *ranges, uint32_t *count, uint32_t capacity, uint64_t base, uint64_t len)
{
    if (!len)
        return;
    if (*count < capacity)
        ranges[(*count)++] = (range_t){base, len};
}

static void range_exclude_from(range_t *ranges, uint32_t *count, uint32_t capacity, uint64_t ebase, uint64_t elen)
{
    if (!elen)
        return;
    for (uint32_t i = 0; i < *count;)
    {
        uint64_t b = ranges[i].base, e = b + ranges[i].len;
        uint64_t xb = ebase, xe = ebase + elen;
        if (xe <= b || e <= xb)
        {
            i++;
            continue;
        }

        range_t left = {b, xb > b ? xb - b : 0};
        range_t right = {xe < e ? xe : 0, xe < e ? e - xe : 0};

        if (left.len)
        {
            ranges[i] = left;
            i++;
        }
        else
        {
            ranges[i] = ranges[--(*count)];
        }
        range_append(ranges, count, capacity, right.base, right.len);
    }
}

/* We learn the VA←→PA delta from the framebuffer mapping */
extern volatile uint32_t *g_fb32;       // VA the kernel draws into
extern const boot_info *k_bootinfo_ptr; // has fb.fb_base (PA)

/* Global VA-PA delta (VA = PA + g_pa2va_delta). 0 = unknown/identity. */
static uint64_t g_pa2va_delta = 0;

static void range_exclude(uint64_t ebase, uint64_t elen)
{
    range_exclude_from(pool, &pool_count, (uint32_t)(sizeof(pool) / sizeof(pool[0])), ebase, elen);
    range_exclude_from(pool_exec, &pool_exec_count, (uint32_t)(sizeof(pool_exec) / sizeof(pool_exec[0])), ebase, elen);
}

#ifndef phys_to_virt
// Fallback: assume identity map (can be overridden later)
static inline void *phys_to_virt(uint64_t phys)
{
    return (void *)(uintptr_t)(phys + g_pa2va_delta);
}
#endif

void *pmem_phys_to_virt(uint64_t phys)
{
    return phys_to_virt(phys);
}

uint64_t pmem_virt_to_phys(const void *virt)
{
    uint64_t va = (uint64_t)(uintptr_t)virt;
    if (g_pa2va_delta && va >= g_pa2va_delta)
        return va - g_pa2va_delta;
    return va;
}

// --- <4GB split pools for DMA-constrained devices -------------------------
static range_t pool_lo[256], pool_hi[256];
static uint32_t pool_lo_count = 0, pool_hi_count = 0;

static inline void range_add_to(range_t *dst, uint32_t *cnt, uint64_t base, uint64_t len)
{
    if (!len)
        return;
    if (*cnt < 256)
        dst[(*cnt)++] = (range_t){base, len};
}

static void split_into_lo_hi(void)
{
    const uint64_t LIM4G = 0x100000000ULL; // 4GB
    pool_lo_count = pool_hi_count = 0;

    for (uint32_t i = 0; i < pool_count; ++i)
    {
        uint64_t b = pool[i].base, e = b + pool[i].len;
        if (e <= LIM4G)
        {
            range_add_to(pool_lo, &pool_lo_count, b, e - b);
        }
        else if (b >= LIM4G)
        {
            range_add_to(pool_hi, &pool_hi_count, b, e - b);
        }
        else
        {
            // crosses 4GB: split
            range_add_to(pool_lo, &pool_lo_count, b, LIM4G - b);
            range_add_to(pool_hi, &pool_hi_count, LIM4G, e - LIM4G);
        }
    }
}

static void *pool_alloc(range_t *dst, uint32_t *cnt, uint64_t n_pages)
{
    const uint64_t need = n_pages * PAGE_SIZE;
    for (uint32_t i = 0; i < *cnt; ++i)
    {
        if (dst[i].len >= need)
        {
            uint64_t out = dst[i].base;
            dst[i].base += need;
            dst[i].len -= need;
            return phys_to_virt(out);
        }
    }
    return NULL;
}

static inline void pmem_try_learn_va_delta(void)
{
    if (!g_pa2va_delta && k_bootinfo_ptr && g_fb32)
    {
        uint64_t fb_pa = k_bootinfo_ptr->fb.fb_base;  // physical
        uint64_t fb_va = (uint64_t)(uintptr_t)g_fb32; // virtual
        if (fb_pa && fb_va)
        {
            g_pa2va_delta = fb_va - fb_pa;
        }
    }
}

void pmem_init(const boot_info *bi)
{
    pool_count = 0;
    pool_exec_count = 0;

    pmem_try_learn_va_delta();

    // 1) read the firmware memory map
    uint8_t *p = (uint8_t *)(uintptr_t)bi->mmap;
    uint8_t *end = p + bi->mmap_size;
    uint64_t dsz = bi->mmap_desc_size ? bi->mmap_desc_size : sizeof(efi_mdesc);

    for (; p + dsz <= end; p += dsz)
    {
        const efi_mdesc *d = (const efi_mdesc *)p;
        uint64_t base = d->PhysicalStart;
        uint64_t size = d->NumberOfPages * PAGE_SIZE;

        // normalise and store
        if (size && is_reclaimable_type(d->Type))
            range_add(base, size);
    }

    // Stage2 reserves this as EfiLoaderCode before ExitBootServices, so it is
    // writable for loading and executable for direct AArch64 app entry.
    if (bi->sacx_exec_pool_base_phys && bi->sacx_exec_pool_size_bytes)
        range_add_exec(bi->sacx_exec_pool_base_phys, bi->sacx_exec_pool_size_bytes);

    // 2) exclude regions we must not touch:
    //    - the kernel image itself
    range_exclude(bi->kernel_base_phys, bi->kernel_size_bytes);

    //    - the framebuffer
    range_exclude(bi->fb.fb_base, (uint64_t)bi->fb.pitch * bi->fb.height);

    //    - the memory-map buffer (so we don't allocate over it)
    range_exclude(bi->mmap, bi->mmap_size);

    //    - firmware blobs passed by stage2
    for (uint32_t i = 0; i < bi->wifi_fw_count && i < BOOTINFO_WIFI_FW_MAX; ++i)
        range_exclude(bi->wifi_fw[i].base_phys, bi->wifi_fw[i].size_bytes);

    // 3) page-align all ranges (down/up)
    for (uint32_t i = 0; i < pool_count; ++i)
    {
        uint64_t b = align_up(pool[i].base, PAGE_SIZE);
        uint64_t e = align_down(pool[i].base + pool[i].len, PAGE_SIZE);
        if (e > b)
        {
            pool[i].base = b;
            pool[i].len = e - b;
        }
        else
        {
            pool[i] = pool[--pool_count];
            i--;
        }
    }
    for (uint32_t i = 0; i < pool_exec_count; ++i)
    {
        uint64_t b = align_up(pool_exec[i].base, PAGE_SIZE);
        uint64_t e = align_down(pool_exec[i].base + pool_exec[i].len, PAGE_SIZE);
        if (e > b)
        {
            pool_exec[i].base = b;
            pool_exec[i].len = e - b;
        }
        else
        {
            pool_exec[i] = pool_exec[--pool_exec_count];
            i--;
        }
    }
    for (uint32_t i = 0; i < pool_exec_count; ++i)
    {
        range_exclude_from(pool, &pool_count, (uint32_t)(sizeof(pool) / sizeof(pool[0])),
                           pool_exec[i].base, pool_exec[i].len);
    }

    // 3b) split the usable memory into <4GB (low-DMA) and >=4GB pools
    split_into_lo_hi();
}

// Allocate pages guaranteed below 4GB (for 32-bit DMA devices like some xHCI)
void *pmem_alloc_pages_lowdma(uint64_t n_pages)
{
    if (!n_pages)
        return NULL;
    // strictly from the <4GB pool
    return pool_alloc(pool_lo, &pool_lo_count, n_pages);
}

// Compatibility wrapper used by USB/xHCI code.
// Allocates DMA-safe memory guaranteed below 4GB.
void *alloc_dma(uint32_t pages)
{
    return pmem_alloc_pages_lowdma((uint64_t)pages);
}

void *pmem_alloc_pages(uint64_t n_pages)
{
    if (!n_pages)
        return NULL;
    void *p = pool_alloc(pool_hi, &pool_hi_count, n_pages);
    if (p)
        return p;
    return pool_alloc(pool_lo, &pool_lo_count, n_pages);
}

void *pmem_alloc_executable_pages(uint64_t n_pages)
{
    if (!n_pages)
        return NULL;
    return pool_alloc(pool_exec, &pool_exec_count, n_pages);
}

void pmem_free_executable_pages(void *p, uint64_t n_pages)
{
    uint64_t base = 0;
    uint64_t len = 0;

    if (!p || !n_pages)
        return;

    base = (uint64_t)(uintptr_t)p;
    if (g_pa2va_delta && base >= g_pa2va_delta)
        base -= g_pa2va_delta;

    len = n_pages * PAGE_SIZE;
    range_add_exec(base, len);
}

void pmem_free_pages(void *p, uint64_t n)
{
    uint64_t base = 0;
    uint64_t len = 0;
    const uint64_t LIM4G = 0x100000000ULL;

    if (!p || !n)
        return;

    base = (uint64_t)(uintptr_t)p;
    if (g_pa2va_delta && base >= g_pa2va_delta)
        base -= g_pa2va_delta;

    len = n * PAGE_SIZE;
    range_add(base, len);

    if (base < LIM4G)
    {
        uint64_t lo_len = (base + len > LIM4G) ? (LIM4G - base) : len;
        range_add_to(pool_lo, &pool_lo_count, base, lo_len);
        if (lo_len < len)
            range_add_to(pool_hi, &pool_hi_count, LIM4G, len - lo_len);
    }
    else
    {
        range_add_to(pool_hi, &pool_hi_count, base, len);
    }
}
