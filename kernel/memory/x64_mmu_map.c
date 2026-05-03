#include "memory/x64_mmu_map.h"

#if defined(DIHOS_ARCH_X64) || defined(KERNEL_ARCH_X64) || defined(__x86_64__) || defined(_M_X64)

#include "memory/pmem.h"
#include "hardware_probes/acpi_probe_pci_lookup.h"
#include "terminal/terminal_api.h"
#include <stdint.h>
#include <stddef.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

#define PAGE_SIZE_4K 4096ull
#define PT_ENTRIES   512u

#define X64_PTE_P       (1ull << 0)
#define X64_PTE_RW      (1ull << 1)
#define X64_PTE_US      (1ull << 2)
#define X64_PTE_PWT     (1ull << 3)
#define X64_PTE_PCD     (1ull << 4)
#define X64_PTE_A       (1ull << 5)
#define X64_PTE_D       (1ull << 6)
#define X64_PTE_PS      (1ull << 7)
#define X64_PTE_G       (1ull << 8)

#define X64_ADDR_MASK   0x000FFFFFFFFFF000ull

static inline uint64_t read_cr3(void)
{
    uint64_t v;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline uint64_t read_cr4(void)
{
    uint64_t v;
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(v));
    return v;
}

static inline void write_cr3(uint64_t v)
{
    __asm__ __volatile__("mov %0, %%cr3" :: "r"(v) : "memory");
}

static uint64_t align_down(uint64_t x, uint64_t a) { return x & ~(a - 1ull); }
static uint64_t align_up(uint64_t x, uint64_t a) { return (x + a - 1ull) & ~(a - 1ull); }

static void zero_page(void *p)
{
    uint64_t *q = (uint64_t*)p;
    for (uint32_t i = 0; i < PT_ENTRIES; ++i)
        q[i] = 0;
}

static int x64_la57_enabled(void)
{
    return (read_cr4() & (1ull << 12)) != 0ull;
}

static uint64_t root_table_phys(void)
{
    return read_cr3() & X64_ADDR_MASK;
}

static uint64_t table_phys_from_desc(uint64_t desc)
{
    return desc & X64_ADDR_MASK;
}

static uint64_t make_table_desc(uint64_t table_phys)
{
    return (table_phys & X64_ADDR_MASK) | X64_PTE_P | X64_PTE_RW;
}

static uint64_t make_mmio_page_desc(uint64_t phys)
{
    return (phys & X64_ADDR_MASK) |
           X64_PTE_P | X64_PTE_RW |
           X64_PTE_PCD | X64_PTE_PWT |
           X64_PTE_A | X64_PTE_D;
}

static uint32_t idx_pml5(uint64_t va) { return (uint32_t)((va >> 48) & 0x1FFull); }
static uint32_t idx_pml4(uint64_t va) { return (uint32_t)((va >> 39) & 0x1FFull); }
static uint32_t idx_pdpt(uint64_t va) { return (uint32_t)((va >> 30) & 0x1FFull); }
static uint32_t idx_pd(uint64_t va)   { return (uint32_t)((va >> 21) & 0x1FFull); }
static uint32_t idx_pt(uint64_t va)   { return (uint32_t)((va >> 12) & 0x1FFull); }

static uint64_t huge_size_for_level(int level)
{
    /* level: 2 = PDPT 1GiB, 3 = PD 2MiB */
    if (level == 2)
        return 1ull << 30;
    if (level == 3)
        return 1ull << 21;
    return 0;
}

static uint64_t *split_existing_huge_mapping(uint64_t *table, uint32_t idx, int level)
{
    uint64_t old_desc = table[idx];
    uint64_t old_pa;
    uint64_t child_size;
    uint64_t attrs;
    void *new_page;
    uint64_t *child;

    if (!(old_desc & X64_PTE_P) || !(old_desc & X64_PTE_PS))
        return NULL;

    if (level != 2 && level != 3)
        return NULL;

    new_page = pmem_alloc_pages(1);
    if (!new_page)
        return NULL;

    zero_page(new_page);
    child = (uint64_t *)new_page;

    old_pa = align_down(table_phys_from_desc(old_desc), huge_size_for_level(level));
    attrs = old_desc & ~X64_ADDR_MASK;

    if (level == 2)
    {
        /* Split 1GiB PDPT huge page into 512x 2MiB PD huge pages. */
        child_size = 1ull << 21;
        for (uint32_t i = 0; i < PT_ENTRIES; ++i)
            child[i] = ((old_pa + ((uint64_t)i * child_size)) & X64_ADDR_MASK) | attrs;
    }
    else
    {
        /* Split 2MiB PD huge page into 512x 4KiB PT pages. */
        child_size = PAGE_SIZE_4K;
        attrs &= ~X64_PTE_PS;
        for (uint32_t i = 0; i < PT_ENTRIES; ++i)
            child[i] = ((old_pa + ((uint64_t)i * child_size)) & X64_ADDR_MASK) | attrs;
    }

    table[idx] = make_table_desc((uint64_t)(uintptr_t)new_page);

    terminal_print("x64 MMU: split existing huge mapping level ");
    terminal_print_inline_hex64((uint64_t)level);

    return child;
}

static uint64_t *get_or_create_next_table(uint64_t *table, uint32_t idx, int level)
{
    uint64_t desc = table[idx];

    if ((desc & X64_PTE_P) && !(desc & X64_PTE_PS))
        return (uint64_t *)(uintptr_t)table_phys_from_desc(desc);

    if (desc & X64_PTE_P)
        return split_existing_huge_mapping(table, idx, level);

    void *new_page = pmem_alloc_pages(1);
    if (!new_page)
        return NULL;

    zero_page(new_page);
    table[idx] = make_table_desc((uint64_t)(uintptr_t)new_page);
    return (uint64_t *)new_page;
}

static int map_one_4k(uint64_t va, uint64_t pa)
{
    uint64_t *table = (uint64_t *)(uintptr_t)root_table_phys();

    if (!table)
        return -1;

    if (x64_la57_enabled())
    {
        table = get_or_create_next_table(table, idx_pml5(va), 0);
        if (!table)
            return -2;
    }

    table = get_or_create_next_table(table, idx_pml4(va), 1);
    if (!table)
        return -3;

    table = get_or_create_next_table(table, idx_pdpt(va), 2);
    if (!table)
        return -4;

    table = get_or_create_next_table(table, idx_pd(va), 3);
    if (!table)
        return -5;

    table[idx_pt(va)] = make_mmio_page_desc(pa);
    return 0;
}

void x64_mmu_print_state(void)
{
    terminal_print("x64 MMU state:");
    terminal_print(" CR3: ");
    terminal_print_inline_hex64(read_cr3());
    terminal_print(" CR4: ");
    terminal_print_inline_hex64(read_cr4());
    terminal_print(" LA57: ");
    terminal_print_inline_hex64((uint64_t)x64_la57_enabled());
}

int x64_mmu_map_device_identity(uint64_t phys, uint64_t size)
{
    if (!phys || !size)
        return -1;

    uint64_t start = align_down(phys, PAGE_SIZE_4K);
    uint64_t end = align_up(phys + size, PAGE_SIZE_4K);
    uint64_t cr3 = read_cr3();

    terminal_print("x64 MMIO identity map: ");
    terminal_print_inline_hex64(start);
    terminal_print(" size: ");
    terminal_print_inline_hex64(end - start);

    for (uint64_t p = start; p < end; p += PAGE_SIZE_4K)
    {
        int rc = map_one_4k(p, p);
        if (rc != 0)
        {
            terminal_print("x64 MMIO map failed at: ");
            terminal_print_inline_hex64(p);
            terminal_print(" rc: ");
            terminal_print_inline_hex64((uint64_t)(int64_t)rc);
            write_cr3(cr3);
            return rc;
        }
    }

    write_cr3(cr3);
    return 0;
}

int x64_mmu_map_pci_ecams_from_rsdp(uint64_t rsdp_phys)
{
    dihos_pci_ecam ecams[DIHOS_PCI_ECAM_MAX];
    uint32_t count = acpi_pci_get_ecams_from_rsdp(rsdp_phys, ecams, DIHOS_PCI_ECAM_MAX);
    int worst = 0;

    terminal_print("x64 mapping PCI ECAMs as uncached MMIO");
    terminal_print("ECAM count: ");
    terminal_print_inline_hex64(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        uint64_t buses = (uint64_t)ecams[i].end_bus - (uint64_t)ecams[i].start_bus + 1ull;
        uint64_t size = buses << 20;
        int rc;

        terminal_print("ECAM seg: ");
        terminal_print_inline_hex64(ecams[i].segment);
        terminal_print(" base: ");
        terminal_print_inline_hex64(ecams[i].base);
        terminal_print(" bytes: ");
        terminal_print_inline_hex64(size);

        rc = x64_mmu_map_device_identity(ecams[i].base, size);
        if (rc != 0)
            worst = rc;
    }

    return worst;
}

#endif
