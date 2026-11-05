#pragma once
#include <stdint.h>
// Native WebCore + video currently needs 273301 relative fixups. Admission
// remains indexed and limited to 256 records per service step.
// Keep independent bounded file/image/relocation ceilings; no dynamic linker.
#define DIHOS_USER_ELF_MAX_BYTES (128u*1024u*1024u)
#define DIHOS_USER_ELF_MAX_IMAGE_BYTES (128u*1024u*1024u)
#define DIHOS_USER_ELF_INLINE_RELOCATIONS 256u
#define DIHOS_USER_ELF_MAX_SEGMENTS 8u
#define DIHOS_USER_ELF_MAX_RELOCATIONS 524288u
typedef struct dihos_user_elf_relocation {uint64_t address,addend;} dihos_user_elf_relocation;
typedef struct dihos_user_elf_segment {
    uint64_t offset, address, file_bytes, memory_bytes;
    uint32_t flags;
} dihos_user_elf_segment;
typedef struct dihos_user_elf_plan {
    uint64_t entry, bytes;
    uint32_t count;
    dihos_user_elf_segment segments[DIHOS_USER_ELF_MAX_SEGMENTS];
    uint32_t relocation_count;
    uint64_t tls_offset,tls_file_bytes,tls_memory_bytes,tls_alignment;
    uint64_t tls_address,relocation_offset,source_bytes;
    const uint8_t *source;
    dihos_user_elf_relocation relocations[DIHOS_USER_ELF_INLINE_RELOCATIONS];
} dihos_user_elf_plan;
/* Static ARM64 ET_DYN; optional bounded symbol-free RELATIVE relocations.
 * No interpreter, imports, executable relocations, dynamic TLS or Linux ABI.
 * Bounded local-exec PT_TLS templates are supported. */
int dihos_user_elf_validate(const void *data,uint64_t bytes,dihos_user_elf_plan *out);
/* Large callers retain immutable, kernel-owned source until all steps finish. */
int dihos_user_elf_headers(const void*,uint64_t,dihos_user_elf_plan*);
int dihos_user_elf_validate_step(dihos_user_elf_plan*,unsigned first,unsigned count);
int dihos_user_elf_relocate_step(const dihos_user_elf_plan*,void*,uint64_t,unsigned first,unsigned count);
/* Apply the validated plan to owned, inactive backing before entry. */
int dihos_user_elf_relocate(const dihos_user_elf_plan *plan,void *image,uint64_t bytes);
