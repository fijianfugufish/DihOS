#include "memory/aarch64_user_vm.h"

#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)

#include "memory/pmem.h"
#include "asm/asm.h"
#include "terminal/terminal_api.h"

#define A64_PT_ENTRIES 512u
#define A64_DESC_VALID (1ull << 0)
#define A64_DESC_TABLE (1ull << 1)
#define A64_DESC_PAGE  (1ull << 1)
#define A64_DESC_AF    (1ull << 10)
#define A64_DESC_PXN   (1ull << 53)
#define A64_DESC_UXN   (1ull << 54)
#define A64_DESC_CONTIGUOUS (1ull << 52)
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

/* Do not inherit firmware mappings that already grant EL0 access. Cloning
 * such a map would expose unrelated memory before the loader maps anything.
 * The walk is read-only, bounded, and also rejects unsupported TTBR1 layouts. */
static int vm_kernel_only(uint64_t physical,uint32_t level,uint32_t *budget)
{
    if(!physical || !*budget)return -1;
    --*budget;
    const uint64_t *table=(const uint64_t*)pmem_phys_to_virt(physical);
    if(!table)return -1;
    for(uint32_t i=0;i<512;++i){
        uint64_t desc=table[i];if(!(desc&1))continue;
        if(level<3 && (desc&2)){
            if(vm_kernel_only(desc&A64_ADDR_MASK,level+1,budget))return -1;
        }else if(desc&(1ull<<6))return -1;
    }
    return 0;
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

static uint64_t *vm_clone_table(aarch64_user_vm *vm, const uint64_t *source,uint32_t level)
{
    uint64_t *table = vm_alloc_table(vm);

    if (!table || !source)
        return 0;
    for (uint32_t i = 0u; i < A64_PT_ENTRIES; ++i)
    {
        uint64_t descriptor=source[i];
        /* A private tree changes individual permissions and can replace a
         * block with a table. Retaining a firmware contiguous group would
         * leave inconsistent descriptors within that group. Unfold every
         * leaf hint in the cloned table, without touching firmware tables. */
        if((descriptor&A64_DESC_VALID) && (level==3 || !(descriptor&A64_DESC_TABLE)))descriptor&=~A64_DESC_CONTIGUOUS;
        table[i] = descriptor;
    }
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
           A64_DESC_PXN | execute_never | (1ull<<11); /* User entries are non-global. */
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
        attrs = desc & ~(A64_ADDR_MASK|A64_DESC_CONTIGUOUS);
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
                                                                A64_ADDR_MASK),level+1);
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
    uint32_t walk_budget=4096;
    if(vm_kernel_only(read_ttbr0_el1()&A64_ADDR_MASK,vm_start_level(va_bits),&walk_budget))return -6;
    if(!(tcr&(1ull<<23))){
        uint64_t ttbr1;__asm__ volatile("mrs %0,ttbr1_el1":"=r"(ttbr1));
        if(ttbr1&A64_ADDR_MASK){
            uint32_t bits=64u-(uint32_t)((tcr>>16)&63);
            if(((tcr>>30)&3)!=2 || bits<39 || bits>48 ||
               vm_kernel_only(ttbr1&A64_ADDR_MASK,vm_start_level(bits),&walk_budget))return -6;
        }
    }
    vm->va_bits = va_bits;
    root = vm_clone_table(vm, source_root,vm_start_level(va_bits));
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

int aarch64_user_vm_prepare_owned(aarch64_user_vm *vm,uint64_t va,uint64_t physical,uint64_t bytes)
{
    if(!vm || !vm->root_phys || !bytes || ((va|physical|bytes)&4095) ||
       vm->va_bits<39 || vm->va_bits>48 || va>=(1ull<<vm->va_bits) ||
       bytes>(1ull<<vm->va_bits)-va || physical>UINT64_MAX-bytes)return -22;
    /* Only kernel-allocated backing may be prepared; no arbitrary overwrite. */
    for(uint64_t n=0;n<bytes;n+=4096)
        if(!vm_address_maps_exact_physical(vm,va+n,physical+n))return -14;
    struct {uint64_t *at,old;} undo[AARCH64_USER_VM_MAX_TABLES];
    uint32_t changes=0,initial=vm->owned_table_count;
    for(uint64_t n=0;n<bytes;n+=4096){
        uint64_t *table=pmem_phys_to_virt(vm->root_phys);
        for(uint32_t level=vm_start_level(vm->va_bits);level<3;++level){
            uint32_t index=level_index(va+n,level);uint64_t *at=&table[index],old=*at;
            table=vm_private_child(vm,table,index,level);
            if(*at!=old){undo[changes].at=at;undo[changes++].old=old;}
            if(!table)goto rollback;
        }
    }
    return 0;
rollback:
    while(changes){--changes;*undo[changes].at=undo[changes].old;}
    while(vm->owned_table_count>initial){
        --vm->owned_table_count;pmem_free_pages(vm->owned_tables[vm->owned_table_count],1);
        vm->owned_tables[vm->owned_table_count]=0;
    }
    return -12;
}
int aarch64_user_vm_set_owned(aarch64_user_vm *vm,uint64_t va,uint64_t physical,uint64_t bytes,uint32_t permissions)
{
    if(!vm || !vm->root_phys || !bytes || ((va|physical|bytes)&4095) ||
       vm->va_bits<39 || vm->va_bits>48 || va>=(1ull<<vm->va_bits) ||
       bytes>(1ull<<vm->va_bits)-va || physical>UINT64_MAX-bytes ||
       (permissions!=0 && permissions!=AARCH64_USER_VM_READ &&
        permissions!=(AARCH64_USER_VM_READ|AARCH64_USER_VM_WRITE)))return -22;
    /* Validate every page before changing any permissions; no allocation here. */
    for(uint64_t n=0;n<bytes;n+=4096){
        const uint64_t *table=pmem_phys_to_virt(vm->root_phys);
        for(uint32_t level=vm_start_level(vm->va_bits);level<3;++level){
            uint64_t d=table[level_index(va+n,level)];
            if((d&3)!=3 || !table_is_owned(vm,d&A64_ADDR_MASK))return -14;
            table=pmem_phys_to_virt(d&A64_ADDR_MASK);
        }
        uint64_t d=table[level_index(va+n,3)];
        if((d&3)!=3 || (d&A64_ADDR_MASK)!=physical+n)return -14;
    }
    for(uint64_t n=0;n<bytes;n+=4096){
        uint64_t *table=pmem_phys_to_virt(vm->root_phys);
        for(uint32_t level=vm_start_level(vm->va_bits);level<3;++level)
            table=pmem_phys_to_virt(table[level_index(va+n,level)]&A64_ADDR_MASK);
        uint64_t d=make_user_page_desc(physical+n,permissions,vm->normal_memory_attr_index);
        if(!permissions)d&=~(3ull<<6); /* EL1 RW, EL0 inaccessible, NX. */
        table[level_index(va+n,3)]=d;
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

int aarch64_user_vm_publish(const aarch64_user_vm *vm)
{
    if(!vm || !vm->root_phys || !vm->owned_table_count ||
       vm->owned_table_count>AARCH64_USER_VM_MAX_TABLES || !table_is_owned(vm,vm->root_phys))return -1;
    /* Inert tree: publish children before parents, ending with the root.
     * DSB alone orders writes but does not clean dirty descriptor lines for
     * a non-cacheable/non-coherent table walker. */
    for(uint32_t i=vm->owned_table_count;i>0;--i)asm_dma_clean_range(vm->owned_tables[i-1],4096);
    __asm__ volatile("dsb sy; isb":::"memory");
    return 0;
}
int aarch64_user_vm_describe(const aarch64_user_vm *vm,uint64_t va,uint64_t descriptors[4])
{
    if(!descriptors)return -1;
    for(unsigned i=0;i<4;++i)descriptors[i]=0;
    if(!vm || !vm->root_phys || vm->va_bits<39 || vm->va_bits>48 || va>=(1ull<<vm->va_bits))return -1;
    const uint64_t *table=pmem_phys_to_virt(vm->root_phys);
    for(uint32_t level=vm_start_level(vm->va_bits);level<=3 && table;++level){
        uint64_t d=table[level_index(va,level)];descriptors[level]=d;
        if(!(d&A64_DESC_VALID) || level==3 || !(d&A64_DESC_TABLE))return (int)level;
        table=pmem_phys_to_virt(d&A64_ADDR_MASK);
    }
    return -1;
}
static int user_copy_pa(const aarch64_user_vm *vm,uint64_t va,int write,uint64_t *pa){
    uint64_t d[4];int level=aarch64_user_vm_describe(vm,va,d);
    if(level!=3 || (d[3]&3)!=3 || !(d[3]&(1ull<<6)) || (write && (d[3]&(1ull<<7))))return -14;
    for(unsigned i=0;i<3;++i)if(d[i] && ((d[i]&(1ull<<61)) || (write && (d[i]&(1ull<<62)))))return -14;
    *pa=(d[3]&A64_ADDR_MASK)+(va&4095);return 0;
}
int aarch64_user_vm_check_execute(const aarch64_user_vm *vm,uint64_t address){
    uint64_t d[4];int level=aarch64_user_vm_describe(vm,address,d);
    if((address&3) || level!=3 || (d[3]&3)!=3 || !(d[3]&(1ull<<6)) || (d[3]&(1ull<<54)))return -14;
    for(unsigned i=0;i<3;++i)if(d[i]&((1ull<<60)|(1ull<<61)))return -14;
    return 0;
}
int aarch64_user_vm_check_buffer(const aarch64_user_vm *vm,uint64_t user,uint64_t bytes,int write){
    if(bytes>4096)return -7;
    if(!bytes)return 0;
    if(!vm || vm->va_bits<39 || vm->va_bits>48 ||
       user>=(1ull<<vm->va_bits) || bytes>(1ull<<vm->va_bits)-user)return -14;
    /* First pass prevents partial output if the second page is a guard. */
    for(uint64_t offset=0;offset<bytes;){uint64_t pa;int rc=user_copy_pa(vm,user+offset,write,&pa);if(rc)return rc;
        uint64_t n=4096-((user+offset)&4095);if(n>bytes-offset)n=bytes-offset;offset+=n;
    }
    return 0;
}
static int user_copy(const aarch64_user_vm *vm,uint64_t user,void *kernel,uint64_t bytes,int write){
    int check=aarch64_user_vm_check_buffer(vm,user,bytes,write);if(check)return check;
    if(bytes && !kernel)return -14;
    for(uint64_t offset=0;offset<bytes;){uint64_t pa;int rc=user_copy_pa(vm,user+offset,write,&pa);if(rc)return rc;
        uint8_t *backing=pmem_phys_to_virt(pa);if(!backing)return -14;
        uint64_t n=4096-((user+offset)&4095);if(n>bytes-offset)n=bytes-offset;
        for(uint64_t i=0;i<n;++i){if(write)backing[i]=((const uint8_t*)kernel)[offset+i];else ((uint8_t*)kernel)[offset+i]=backing[i];}
        offset+=n;
    }
    return 0;
}
int aarch64_user_vm_copy_in(const aarch64_user_vm *vm,void *kernel,uint64_t user,uint64_t bytes){return user_copy(vm,user,kernel,bytes,0);}
int aarch64_user_vm_copy_out(const aarch64_user_vm *vm,uint64_t user,const void *kernel,uint64_t bytes){return user_copy(vm,user,(void*)kernel,bytes,1);}
#include "aarch64_user_sparse.inc"

#else
/* The ARM64 user runtime is unavailable on other architectures. Keep the
 * shared service linkable without admitting unchecked copies there. */
int aarch64_user_vm_copy_in(const aarch64_user_vm *vm,void *kernel,uint64_t user,uint64_t bytes){(void)vm;(void)kernel;(void)user;(void)bytes;return -38;}
int aarch64_user_vm_copy_out(const aarch64_user_vm *vm,uint64_t user,const void *kernel,uint64_t bytes){(void)vm;(void)kernel;(void)user;(void)bytes;return -38;}
int aarch64_user_vm_check_buffer(const aarch64_user_vm *vm,uint64_t user,uint64_t bytes,int write){(void)vm;(void)user;(void)bytes;(void)write;return -38;}
int aarch64_user_vm_check_execute(const aarch64_user_vm *vm,uint64_t address){(void)vm;(void)address;return -38;}
#endif
