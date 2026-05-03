#include "memory/aarch64_mmu_map.h"

#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)

#include "memory/pmem.h"
#include "hardware_probes/acpi_probe_pci_lookup.h"
#include "terminal/terminal_api.h"
#include "bootinfo.h"
#include <stdint.h>
#include <stddef.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

#define PAGE_SIZE_4K 4096ull
#define PT_ENTRIES   512u

#define DESC_VALID   (1ull << 0)
#define DESC_TABLE   (1ull << 1) /* also PAGE at level 3 */
#define DESC_PAGE    (1ull << 1)
#define DESC_AF      (1ull << 10)
#define DESC_PXN     (1ull << 53)
#define DESC_UXN     (1ull << 54)

#define ATTRIDX_DEVICE 1ull
#define SH_OUTER       (2ull << 8)
#define AP_RW_EL1      (0ull << 6)

#define A64_ADDR_MASK  0x0000FFFFFFFFF000ull

extern const boot_info *k_bootinfo_ptr;

static inline uint64_t read_ttbr0_el1(void)
{
    uint64_t v;
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(v));
    return v;
}

static inline uint64_t read_tcr_el1(void)
{
    uint64_t v;
    __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(v));
    return v;
}

static inline uint64_t read_mair_el1(void)
{
    uint64_t v;
    __asm__ __volatile__("mrs %0, mair_el1" : "=r"(v));
    return v;
}

static inline void write_mair_el1(uint64_t v)
{
    __asm__ __volatile__("msr mair_el1, %0" :: "r"(v) : "memory");
}

static inline void tlbi_all(void)
{
    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("tlbi vmalle1" ::: "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

static uint64_t align_down(uint64_t x, uint64_t a) { return x & ~(a - 1ull); }
static uint64_t align_up(uint64_t x, uint64_t a) { return (x + a - 1ull) & ~(a - 1ull); }

static int ranges_overlap(uint64_t a_base, uint64_t a_size,
                          uint64_t b_base, uint64_t b_size)
{
    uint64_t a_end;
    uint64_t b_end;

    if (!a_size || !b_size)
        return 0;

    a_end = a_base + a_size;
    b_end = b_base + b_size;

    if (a_end < a_base)
        a_end = ~0ull;
    if (b_end < b_base)
        b_end = ~0ull;

    return (a_base < b_end) && (b_base < a_end);
}

static int ecam_overlaps_critical_boot_ranges(uint64_t base, uint64_t size)
{
    const boot_info *bi = k_bootinfo_ptr;

    if (!bi || !size)
        return 0;

    if (ranges_overlap(base, size, bi->kernel_base_phys, bi->kernel_size_bytes))
    {
        terminal_print("ECAM map skip: overlaps kernel image");
        terminal_print(" map base: ");
        terminal_print_inline_hex64(base);
        terminal_print(" map size: ");
        terminal_print_inline_hex64(size);
        terminal_print(" kernel base: ");
        terminal_print_inline_hex64(bi->kernel_base_phys);
        terminal_print(" kernel size: ");
        terminal_print_inline_hex64(bi->kernel_size_bytes);
        return 1;
    }

    if (ranges_overlap(base, size, bi->fb.fb_base, bi->fb.fb_size))
    {
        terminal_print("ECAM map skip: overlaps framebuffer");
        terminal_print(" map base: ");
        terminal_print_inline_hex64(base);
        terminal_print(" map size: ");
        terminal_print_inline_hex64(size);
        terminal_print(" fb base: ");
        terminal_print_inline_hex64(bi->fb.fb_base);
        terminal_print(" fb size: ");
        terminal_print_inline_hex64(bi->fb.fb_size);
        return 1;
    }

    if (ranges_overlap(base, size, bi->mmap, bi->mmap_size))
    {
        terminal_print("ECAM map skip: overlaps memory map copy");
        terminal_print(" map base: ");
        terminal_print_inline_hex64(base);
        terminal_print(" map size: ");
        terminal_print_inline_hex64(size);
        terminal_print(" mmap base: ");
        terminal_print_inline_hex64(bi->mmap);
        terminal_print(" mmap size: ");
        terminal_print_inline_hex64(bi->mmap_size);
        return 1;
    }

    if (ranges_overlap(base, size, bi->sacx_exec_pool_base_phys, bi->sacx_exec_pool_size_bytes))
    {
        terminal_print("ECAM map skip: overlaps sacx exec pool");
        terminal_print(" map base: ");
        terminal_print_inline_hex64(base);
        terminal_print(" map size: ");
        terminal_print_inline_hex64(size);
        terminal_print(" sacx base: ");
        terminal_print_inline_hex64(bi->sacx_exec_pool_base_phys);
        terminal_print(" sacx size: ");
        terminal_print_inline_hex64(bi->sacx_exec_pool_size_bytes);
        return 1;
    }

    return 0;
}

static void zero_page(void *p)
{
    uint64_t *q = (uint64_t*)p;
    for (uint32_t i = 0; i < PT_ENTRIES; ++i)
        q[i] = 0;
}

static uint64_t table_phys_from_desc(uint64_t desc)
{
    return desc & A64_ADDR_MASK;
}

static uint64_t page_table_base_phys(void)
{
    return read_ttbr0_el1() & A64_ADDR_MASK;
}

static int tcr_looks_4k(void)
{
    uint64_t tcr = read_tcr_el1();
    uint64_t tg0 = (tcr >> 14) & 3ull;
    /* TG0 encoding: 00 = 4 KiB, 01 = 64 KiB, 10 = 16 KiB. */
    return tg0 == 0ull;
}

static int start_level_from_tcr(void)
{
    uint64_t tcr = read_tcr_el1();
    uint32_t t0sz = (uint32_t)(tcr & 0x3Fu);
    uint32_t va_bits = 64u - t0sz;
    uint32_t levels;

    if (va_bits <= 12u)
        return -1;

    levels = (va_bits - 12u + 8u) / 9u;
    if (levels < 1u || levels > 4u)
        return -1;

    return (int)(4u - levels);
}

static uint32_t level_index(uint64_t va, int level)
{
    uint32_t shift = (uint32_t)(39 - (level * 9));
    return (uint32_t)((va >> shift) & 0x1FFull);
}

static uint64_t level_size(int level)
{
    return 1ull << (39 - (level * 9));
}

static uint64_t make_table_desc(uint64_t table_phys)
{
    return (table_phys & A64_ADDR_MASK) | DESC_VALID | DESC_TABLE;
}

static uint64_t make_device_page_desc(uint64_t phys)
{
    return (phys & A64_ADDR_MASK) |
           DESC_VALID | DESC_PAGE |
           (ATTRIDX_DEVICE << 2) |
           AP_RW_EL1 |
           SH_OUTER |
           DESC_AF |
           DESC_PXN | DESC_UXN;
}

static uint64_t make_child_desc_from_block(uint64_t old_desc, uint64_t child_pa, int child_level)
{
    uint64_t attrs = old_desc & ~A64_ADDR_MASK;

    attrs |= DESC_VALID;

    if (child_level == 3)
        attrs = (attrs | DESC_PAGE) & ~0ull;
    else
        attrs &= ~DESC_TABLE; /* block descriptor at level 1/2 */

    return (child_pa & A64_ADDR_MASK) | attrs;
}

static void ensure_mair_device_slot(void)
{
    uint64_t mair = read_mair_el1();
    uint64_t clear = ~(0xFFull << (ATTRIDX_DEVICE * 8ull));
    uint64_t want = 0x04ull << (ATTRIDX_DEVICE * 8ull); /* Device-nGnRE */
    uint64_t next = (mair & clear) | want;

    if (next != mair)
    {
        write_mair_el1(next);
        tlbi_all();
    }
}

static uint64_t *split_existing_block_to_table(uint64_t *table, uint32_t idx, int level)
{
    uint64_t old_desc = table[idx];
    int child_level = level + 1;
    uint64_t child_size;
    uint64_t old_pa;
    void *new_page;
    uint64_t *child;

    if (!(old_desc & DESC_VALID))
        return NULL;

    if (level >= 3)
        return NULL;

    new_page = pmem_alloc_pages(1);
    if (!new_page)
        return NULL;

    terminal_print("AArch64 MMU: alloc split table va: ");
    terminal_print_inline_hex64((uint64_t)(uintptr_t)new_page);
    terminal_print("AArch64 MMU: alloc split table pa: ");
    terminal_print_inline_hex64(pmem_virt_to_phys(new_page));

    zero_page(new_page);
    child = (uint64_t *)new_page;

    child_size = level_size(child_level);
    old_pa = table_phys_from_desc(old_desc);
    old_pa = align_down(old_pa, level_size(level));

    for (uint32_t i = 0; i < PT_ENTRIES; ++i)
        child[i] = make_child_desc_from_block(old_desc, old_pa + ((uint64_t)i * child_size), child_level);

    table[idx] = make_table_desc(pmem_virt_to_phys(new_page));
    tlbi_all();

    terminal_print("AArch64 MMU: split existing block at level ");
    terminal_print_inline_hex64((uint64_t)level);

    return child;
}

static uint64_t *get_or_create_next_table(uint64_t *table, uint32_t idx, int level)
{
    uint64_t desc = table[idx];

    if ((desc & DESC_VALID) && (desc & DESC_TABLE))
        return (uint64_t *)pmem_phys_to_virt(table_phys_from_desc(desc));

    if (desc & DESC_VALID)
        return split_existing_block_to_table(table, idx, level);

    void *new_page = pmem_alloc_pages(1);
    if (!new_page)
        return NULL;

    terminal_print("AArch64 MMU: alloc new table va: ");
    terminal_print_inline_hex64((uint64_t)(uintptr_t)new_page);
    terminal_print("AArch64 MMU: alloc new table pa: ");
    terminal_print_inline_hex64(pmem_virt_to_phys(new_page));

    zero_page(new_page);
    table[idx] = make_table_desc(pmem_virt_to_phys(new_page));
    tlbi_all();
    return (uint64_t *)new_page;
}

static int map_one_4k(uint64_t va, uint64_t pa, int start_level)
{
    uint64_t *table = (uint64_t *)pmem_phys_to_virt(page_table_base_phys());

    if (!table)
        return -1;

    for (int level = start_level; level < 3; ++level)
    {
        uint32_t idx = level_index(va, level);
        table = get_or_create_next_table(table, idx, level);
        if (!table)
            return -2;
    }

    table[level_index(va, 3)] = make_device_page_desc(pa);
    return 0;
}

void aarch64_mmu_print_state(void)
{
    terminal_print("MMU state:");
    terminal_print(" TTBR0_EL1: ");
    terminal_print_inline_hex64(read_ttbr0_el1());
    terminal_print(" TCR_EL1: ");
    terminal_print_inline_hex64(read_tcr_el1());
    terminal_print(" MAIR_EL1: ");
    terminal_print_inline_hex64(read_mair_el1());
    terminal_print(" start level: ");
    terminal_print_inline_hex64((uint64_t)start_level_from_tcr());
}

int aarch64_mmu_map_device_identity(uint64_t phys, uint64_t size)
{
    if (!phys || !size)
        return -1;
    if (!tcr_looks_4k())
    {
        terminal_print("MMU map failed: TG0 is not 4K");
        return -2;
    }

    int start_level = start_level_from_tcr();
    if (start_level < 0 || start_level > 3)
    {
        terminal_print("MMU map failed: unsupported TCR/T0SZ");
        return -3;
    }

    ensure_mair_device_slot();

    uint64_t start = align_down(phys, PAGE_SIZE_4K);
    uint64_t end = align_up(phys + size, PAGE_SIZE_4K);

    terminal_print("MMIO identity map: ");
    terminal_print_inline_hex64(start);
    terminal_print(" size: ");
    terminal_print_inline_hex64(end - start);

    for (uint64_t p = start; p < end; p += PAGE_SIZE_4K)
    {
        int rc = map_one_4k(p, p, start_level);
        if (rc != 0)
        {
            terminal_print("MMIO map failed at: ");
            terminal_print_inline_hex64(p);
            terminal_print(" rc: ");
            terminal_print_inline_hex64((uint64_t)(int64_t)rc);
            tlbi_all();
            return rc;
        }
    }

    tlbi_all();
    return 0;
}

int aarch64_mmu_map_pci_ecams_from_rsdp(uint64_t rsdp_phys)
{
    dihos_pci_ecam ecams[DIHOS_PCI_ECAM_MAX];
    uint32_t count = acpi_pci_get_ecams_from_rsdp(rsdp_phys, ecams, DIHOS_PCI_ECAM_MAX);
    int worst = 0;
    const boot_info *bi = k_bootinfo_ptr;

    terminal_print("Mapping PCI ECAMs as device memory");
    terminal_print("ECAM count: ");
    terminal_print_inline_hex64(count);
    if (bi)
    {
        terminal_print("Boot kernel phys: ");
        terminal_print_inline_hex64(bi->kernel_base_phys);
        terminal_print("Boot kernel size: ");
        terminal_print_inline_hex64(bi->kernel_size_bytes);
        terminal_print("Boot fb phys: ");
        terminal_print_inline_hex64(bi->fb.fb_base);
        terminal_print("Boot fb size: ");
        terminal_print_inline_hex64(bi->fb.fb_size);
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        uint64_t buses = (uint64_t)ecams[i].end_bus - (uint64_t)ecams[i].start_bus + 1ull;
        uint64_t full_size = buses << 20; /* 1 MiB per bus */
        uint64_t map_size = full_size;
        int rc;

        /* Probe mode: only map the first bus window per segment for now. */
        if (map_size > (1ull << 20))
            map_size = (1ull << 20);

        terminal_print("ECAM seg: ");
        terminal_print_inline_hex64(ecams[i].segment);
        terminal_print(" base: ");
        terminal_print_inline_hex64(ecams[i].base);
        terminal_print(" bytes: ");
        terminal_print_inline_hex64(full_size);
        terminal_print(" map bytes: ");
        terminal_print_inline_hex64(map_size);

        if (ecams[i].base < 0x0000000100000000ull)
        {
            terminal_print("ECAM map skip: sub-4G segment in probe mode");
            terminal_print(" seg: ");
            terminal_print_inline_hex64(ecams[i].segment);
            terminal_print(" base: ");
            terminal_print_inline_hex64(ecams[i].base);
            continue;
        }

        if (ecam_overlaps_critical_boot_ranges(ecams[i].base, map_size))
            continue;

        rc = aarch64_mmu_map_device_identity(ecams[i].base, map_size);
        if (rc != 0)
            worst = rc;
    }

    return worst;
}

#endif
