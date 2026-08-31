#pragma once

#include <stdint.h>

#include "memory/aarch64_user_vm.h"
#include "mesart/mesart_trust.h"

/* The loader deliberately accepts one small, static AArch64 PIE.  Mesa's
 * userspace runtime must be built to this contract; it is not a Linux ELF
 * compatibility loader. */
#define MESART_ELF_PAGE_BYTES          4096ull
#define MESART_ELF_MAX_FILE_BYTES      (64ull * 1024ull * 1024ull)
#define MESART_ELF_MAX_PROGRAM_HEADERS 64u
#define MESART_ELF_DEFAULT_BASE_VA     0x0000004000000000ull

typedef struct mesart_loaded_image
{
    /* Owned contiguous backing allocation.  Release only after its VM has
     * been destroyed and no EL0 thread can still execute from the image. */
    void *memory;
    uint64_t pages;
    uint64_t physical_base;
    uint64_t virtual_base;
    uint64_t virtual_bytes;
    uint64_t entry_va;
} mesart_loaded_image;

/* Reads the declared renderer file, re-hashes those exact bytes, validates a
 * static AArch64 ET_DYN image, applies only RELATIVE relocations, and maps
 * its PT_LOAD segments into an otherwise fresh EL0 VM.  image_base_va maps
 * the image's lowest page; zero selects MESART_ELF_DEFAULT_BASE_VA.
 *
 * On failure after a map attempt the supplied VM is intentionally no longer
 * reusable: release it and construct a new VM.  This avoids an unsafe partial
 * unmap path while the current VM API has no unmap primitive. */
int mesart_elf_load_verified(const char *bundle_root,
                             const mesart_manifest_file *renderer_file,
                             aarch64_user_vm *vm,
                             uint64_t image_base_va,
                             mesart_loaded_image *out_image);

void mesart_elf_release_image(mesart_loaded_image *image);
