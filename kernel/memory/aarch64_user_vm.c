#include "memory/aarch64_user_vm.h"

#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)

#include "memory/pmem.h"
#include "terminal/terminal_api.h"

#define A64_PT_ENTRIES 512u
#define A64_DESC_VALID (1ull << 0)
#define A64_DESC_TABLE (1ull << 1)
#define A64_DESC_PAGE  (1ull << 1)
#define A64_DESC_AF    (1ull << 10)
#define A64_DESC_PXN   (1ull << 53)
#define A64_DESC_UXN   (1ull << 54)
#define A64_AP_EL0_RW  (1ull << 6)
#define A64_AP_EL0_RO  (3ull << 6)
#define A64_SH_INNER    (3ull << 8)
#define A64_ADDR_MASK   0x0000FFFFFFFFF000ull

static uint64_t read_ttbr0_el1(void)
{
    uint64_t value;
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(value));
    return value;
}

static uint64_t read_tcr_el1(void)
{
    uint64_t value;
    __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(value));
    return value;
}

static uint64_t read_mair_el1(void)
{
    uint64_t value;
    __asm__ __volatile__("mrs %0, mair_el1" : "=r"(value));
    return value;
}

static uint32_t vm_start_level(uint32_t va_bits)
{
    uint32_t levels = (va_bits - 12u + 8u) / 9u;
    return 4u - levels;
}

static uint32_t level_index(uint64_t va, uint32_t level)
{
    return (uint32_t)((va >> (39u - level * 9u)) & 0x1ffull);
}

static uint64_t level_size(uint32_t level)
{
    return 1ull << (39u - level * 9u);
}

static uint32_t table_is_owned(const aarch64_user_vm *vm, uint64_t phys)
{
    for (uint32_t i = 0u; i < vm->owned_table_count; ++i)
        if (pmem_virt_to_phys(vm->owned_tables[i]) == phys)
            return 1u;
    return 0u;
}

static uint64_t *vm_alloc_table(aarch64_user_vm *vm)
{
    uint64_t *table;

    if (!vm || vm->owned_table_count >= AARCH64_USER_VM_MAX_TABLES)
        return 0;
    table = (uint64_t *)pmem_alloc_pages(1u);
    if (!table)
        return 0;
    for (uint32_t i = 0u; i < A64_PT_ENTRIES; ++i)
        table[i] = 0u;
    vm->owned_tables[vm->owned_table_count++] = table;
    return table;
}

static uint64_t *vm_clone_table(aarch64_user_vm *vm, const uint64_t *source)
{
    uint64_t *table = vm_alloc_table(vm);

    if (!table || !source)
        return 0;
    for (uint32_t i = 0u; i < A64_PT_ENTRIES; ++i)
        table[i] = source[i];
    return table;
}

static uint64_t make_table_desc(uint64_t phys)
{
    return (phys & A64_ADDR_MASK) | A64_DESC_VALID | A64_DESC_TABLE;
}

static uint64_t make_user_page_desc(uint64_t phys, uint32_t permissions,
                                    uint32_t attr_index)
{
    uint64_t access = (permissions & AARCH64_USER_VM_WRITE) ? A64_AP_EL0_RW :
                                                               A64_AP_EL0_RO;
    uint64_t execute_never = (permissions & AARCH64_USER_VM_EXECUTE) ? 0u :
                                                                         A64_DESC_UXN;

    return (phys & A64_ADDR_MASK) | A64_DESC_VALID | A64_DESC_PAGE |
           ((uint64_t)attr_index << 2) | A64_SH_INNER | A64_DESC_AF | access |
           A64_DESC_PXN | execute_never;
}

static int find_normal_memory_attr_index(uint64_t mair, uint32_t *out_index)
{
    if (!out_index)
        return -1;
    /* Prefer normal, inner/outer write-back non-transient RAM (0xFF). */
    for (uint32_t index = 0u; index < 8u; ++index)
        if (((mair >> (index * 8u)) & 0xffu) == 0xffu)
        {
            *out_index = index;
            return 0;
        }
    return -2;
}

static uint32_t vm_address_is_unmapped(const aarch64_user_vm *vm,
                                       uint64_t va)
{
    const uint64_t *table;
    uint32_t start_level;

    if (!vm || !vm->root_phys)
        return 0u;
    table = (const uint64_t *)pmem_phys_to_virt(vm->root_phys);
    start_level = vm_start_level(vm->va_bits);
    for (uint32_t level = start_level; level <= 3u; ++level)
    {
        uint64_t desc = table[level_index(va, level)];
        if (!(desc & A64_DESC_VALID))
            return 1u;
        if (level == 3u || !(desc & A64_DESC_TABLE))
            return 0u;
        table = (const uint64_t *)pmem_phys_to_virt(desc & A64_ADDR_MASK);
        if (!table)
            return 0u;
    }
    return 0u;
}

/* The boot map is normally a large physical identity map.  A user VM may
 * replace precisely its own backing page in its private clone, but never map
 * an arbitrary physical page over an inherited kernel mapping. */
static uint32_t vm_address_maps_exact_physical(const aarch64_user_vm *vm,
                                               uint64_t va, uint64_t physical)
{
    const uint64_t *table;
    uint32_t start_level;

    if (!vm || !vm->root_phys)
        return 0u;
    table = (const uint64_t *)pmem_phys_to_virt(vm->root_phys);
    start_level = vm_start_level(vm->va_bits);
    for (uint32_t level = start_level; level <= 3u; ++level)
    {
        uint64_t desc = table[level_index(va, level)];
        if (!(desc & A64_DESC_VALID))
            return 0u;
        if (level == 3u || !(desc & A64_DESC_TABLE))
            return ((desc & A64_ADDR_MASK) + (va & (level_size(level) - 1u))) ==
                   physical;
        table = (const uint64_t *)pmem_phys_to_virt(desc & A64_ADDR_MASK);
        if (!table)
            return 0u;
    }
    return 0u;
}

static uint64_t *vm_private_child(aarch64_user_vm *vm, uint64_t *parent,
                                  uint32_t index, uint32_t level)
{
    uint64_t desc = parent[index];
    uint64_t *child;

    if (!(desc & A64_DESC_VALID))
    {
        child = vm_alloc_table(vm);
        if (!child)
            return 0;
        parent[index] = make_table_desc(pmem_virt_to_phys(child));
        return child;
    }
    if (!(desc & A64_DESC_TABLE))
    {
        uint64_t attrs;
        uint64_t physical;
        uint64_t child_bytes;
        uint32_t child_level = level + 1u;

        if (level >= 3u)
            return 0;
        child = vm_alloc_table(vm);
        if (!child)
            return 0;
        attrs = desc & ~A64_ADDR_MASK;
        physical = desc & A64_ADDR_MASK;
        child_bytes = level_size(child_level);
        for (uint32_t i = 0u; i < A64_PT_ENTRIES; ++i)
        {
            uint64_t child_desc = physical + (uint64_t)i * child_bytes;
            child_desc |= attrs | A64_DESC_VALID;
            if (child_level == 3u)
                child_desc |= A64_DESC_PAGE;
            else
                child_desc &= ~A64_DESC_TABLE;
            child[i] = child_desc;
        }
        parent[index] = make_table_desc(pmem_virt_to_phys(child));
        return child;
    }
    if (table_is_owned(vm, desc & A64_ADDR_MASK))
        return (uint64_t *)pmem_phys_to_virt(desc & A64_ADDR_MASK);
    child = vm_clone_table(vm,
                           (const uint64_t *)pmem_phys_to_virt(desc &
                                                                A64_ADDR_MASK));
    if (!child)
        return 0;
    parent[index] = make_table_desc(pmem_virt_to_phys(child));
    return child;
}

static int vm_translate_root(uint64_t root_phys, uint32_t va_bits,
                             uint64_t virtual_address,
                             uint64_t *out_physical_address)
{
    uint32_t start_level;
    const uint64_t *table;

    if (!out_physical_address)
        return -1;
    if (va_bits < 39u || va_bits > 48u ||
        virtual_address >= (1ull << va_bits))
        return -2;
    table = (const uint64_t *)pmem_phys_to_virt(root_phys);
    if (!table)
        return -3;
    start_level = vm_start_level(va_bits);
    for (uint32_t level = start_level; level <= 3u; ++level)
    {
        uint64_t desc = table[level_index(virtual_address, level)];
        if (!(desc & A64_DESC_VALID))
            return -4;
        if (level == 3u || !(desc & A64_DESC_TABLE))
        {
            *out_physical_address = (desc & A64_ADDR_MASK) +
                                    (virtual_address &
                                     (level_size(level) - 1u));
            return 0;
        }
        table = (const uint64_t *)pmem_phys_to_virt(desc & A64_ADDR_MASK);
        if (!table)
            return -5;
    }
    return -6;
}

int aarch64_user_vm_translate_current(uint64_t virtual_address,
                                      uint64_t *out_physical_address)
{
    uint64_t tcr = read_tcr_el1();
    uint32_t va_bits;

    if (((tcr >> 14) & 3ull) != 0u)
        return -1;
    va_bits = 64u - (uint32_t)(tcr & 0x3fu);
    return vm_translate_root(read_ttbr0_el1() & A64_ADDR_MASK, va_bits,
                             virtual_address, out_physical_address);
}

int aarch64_user_vm_translate(const aarch64_user_vm *vm,
                              uint64_t virtual_address,
                              uint64_t *out_physical_address)
{
    if (!vm || !vm->root_phys)
        return -1;
    return vm_translate_root(vm->root_phys, vm->va_bits, virtual_address,
                             out_physical_address);
}

int aarch64_user_vm_create(aarch64_user_vm *vm)
{
    uint64_t tcr;
    uint64_t mair;
    uint32_t va_bits;
    uint32_t normal_memory_attr_index;
    uint64_t *source_root;
    uint64_t *root;

    if (!vm)
        return -1;
    *vm = (aarch64_user_vm){0};
    tcr = read_tcr_el1();
    if (((tcr >> 14) & 3ull) != 0u)
        return -2;
    va_bits = 64u - (uint32_t)(tcr & 0x3fu);
    mair = read_mair_el1();
    if (va_bits < 39u || va_bits > 48u ||
        find_normal_memory_attr_index(mair, &normal_memory_attr_index) != 0)
    {
        terminal_print("[K:PROC] EL0 VM config rejected TCR=");
        terminal_print_inline_hex64(tcr);
        terminal_print(" MAIR=");
        terminal_print_inline_hex64(mair);
        terminal_print(" VA-bits=");
        terminal_print_inline_hex64(va_bits);
        terminal_flush_log();
        return -3;
    }
    source_root = (uint64_t *)pmem_phys_to_virt(read_ttbr0_el1() &
                                                 A64_ADDR_MASK);
    if (!source_root)
        return -4;
    vm->va_bits = va_bits;
    root = vm_clone_table(vm, source_root);
    if (!root)
    {
        aarch64_user_vm_release(vm);
        return -5;
    }
    vm->root_phys = pmem_virt_to_phys(root);
    vm->normal_memory_attr_index = normal_memory_attr_index;
    return 0;
}

int aarch64_user_vm_map(aarch64_user_vm *vm, uint64_t user_va,
                        uint64_t physical_base, uint64_t bytes,
                        uint32_t permissions)
{
    uint64_t max_va;
    uint64_t end;
    uint32_t start_level;

    if (!vm || !vm->root_phys || !bytes ||
        (user_va & (AARCH64_USER_VM_PAGE_SIZE - 1u)) ||
        (physical_base & (AARCH64_USER_VM_PAGE_SIZE - 1u)) ||
        (bytes & (AARCH64_USER_VM_PAGE_SIZE - 1u)) ||
        !(permissions & AARCH64_USER_VM_READ) ||
        (permissions & ~(AARCH64_USER_VM_READ | AARCH64_USER_VM_WRITE |
                         AARCH64_USER_VM_EXECUTE)))
        return -1;
    end = user_va + bytes;
    max_va = 1ull << vm->va_bits;
    if (end < user_va || user_va >= max_va || end > max_va)
        return -2;
    for (uint64_t offset = 0u; offset < bytes;
         offset += AARCH64_USER_VM_PAGE_SIZE)
        if (!vm_address_is_unmapped(vm, user_va + offset) &&
            !vm_address_maps_exact_physical(vm, user_va + offset,
                                             physical_base + offset))
            return -3;
    start_level = vm_start_level(vm->va_bits);
    for (uint64_t offset = 0u; offset < bytes;
         offset += AARCH64_USER_VM_PAGE_SIZE)
    {
        uint64_t *table = (uint64_t *)pmem_phys_to_virt(vm->root_phys);
        uint64_t va = user_va + offset;
        for (uint32_t level = start_level; level < 3u; ++level)
        {
            table = vm_private_child(vm, table, level_index(va, level),
                                     level);
            if (!table)
                return -4;
        }
        table[level_index(va, 3u)] = make_user_page_desc(
            physical_base + offset, permissions, vm->normal_memory_attr_index);
    }
    return 0;
}

void aarch64_user_vm_release(aarch64_user_vm *vm)
{
    if (!vm)
        return;
    for (uint32_t i = 0u; i < vm->owned_table_count; ++i)
        pmem_free_pages(vm->owned_tables[i], 1u);
    *vm = (aarch64_user_vm){0};
}

#endif
