#pragma once

#include <stdint.h>

/*
 * An inert per-process EL0 translation table.  It begins as a private clone
 * of the current EL1 map, whose existing leaves remain EL1-only, then adds
 * explicit EL0 mappings.  Creating or mapping a VM never changes TTBR0_EL1;
 * architecture context-switch code will perform that later transition.
 */

#define AARCH64_USER_VM_PAGE_SIZE 4096ull
#define AARCH64_USER_VM_MAX_TABLES 24u

#define AARCH64_USER_VM_READ    (1u << 0)
#define AARCH64_USER_VM_WRITE   (1u << 1)
#define AARCH64_USER_VM_EXECUTE (1u << 2)

typedef struct aarch64_user_vm
{
    uint64_t root_phys;
    uint32_t va_bits;
    uint32_t normal_memory_attr_index;
    void *owned_tables[AARCH64_USER_VM_MAX_TABLES];
    uint32_t owned_table_count;
} aarch64_user_vm;

/* Clones the active EL1 root but does not install or activate the clone. */
int aarch64_user_vm_create(aarch64_user_vm *vm);
/* Maps contiguous physical pages into an otherwise-unmapped EL0 range. */
int aarch64_user_vm_map(aarch64_user_vm *vm, uint64_t user_va,
                        uint64_t physical_base, uint64_t bytes,
                        uint32_t permissions);
/* Resolves one kernel virtual address through the active EL1 root.  Process
 * setup uses this instead of assuming the firmware supplied an identity map. */
int aarch64_user_vm_translate_current(uint64_t virtual_address,
                                      uint64_t *out_physical_address);
/* Resolves an address through one inert process VM; useful for validating a
 * clone before that VM is ever installed in TTBR0_EL1. */
int aarch64_user_vm_translate(const aarch64_user_vm *vm,
                              uint64_t virtual_address,
                              uint64_t *out_physical_address);
/* Releases only private page-table pages; mapped process memory remains owned
 * by the process loader/supervisor.  The VM must not be active. */
void aarch64_user_vm_release(aarch64_user_vm *vm);
