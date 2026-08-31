#include "mesart/mesart_elf.h"

#include <bearssl.h>

#include "asm/asm.h"
#include "kwrappers/kfile.h"
#include "memory/pmem.h"

#define MESART_ELF_PATH_MAX 320u

#define ELF_ET_DYN       3u
#define ELF_EM_AARCH64   183u
#define ELF_PT_LOAD      1u
#define ELF_PT_DYNAMIC   2u
#define ELF_PT_INTERP    3u
#define ELF_PT_TLS       7u
#define ELF_PF_X         1u
#define ELF_PF_W         2u
#define ELF_DT_NULL      0
#define ELF_DT_NEEDED    1
#define ELF_DT_RELA      7
#define ELF_DT_RELASZ    8
#define ELF_DT_RELAENT   9
#define ELF_DT_REL       17
#define ELF_DT_RELSZ     18
#define ELF_DT_PLTRELSZ  2
#define ELF_DT_JMPREL    23
#define ELF_DT_RELR      36
#define ELF_DT_RELRSZ    35
#define ELF_R_AARCH64_RELATIVE 1027u

typedef struct __attribute__((packed)) mesart_elf_header
{
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_headers_offset;
    uint64_t section_headers_offset;
    uint32_t flags;
    uint16_t header_bytes;
    uint16_t program_header_bytes;
    uint16_t program_header_count;
    uint16_t section_header_bytes;
    uint16_t section_header_count;
    uint16_t section_name_index;
} mesart_elf_header;

typedef struct __attribute__((packed)) mesart_elf_program_header
{
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_bytes;
    uint64_t memory_bytes;
    uint64_t alignment;
} mesart_elf_program_header;

typedef struct __attribute__((packed)) mesart_elf_dynamic
{
    int64_t tag;
    uint64_t value;
} mesart_elf_dynamic;

typedef struct __attribute__((packed)) mesart_elf_rela
{
    uint64_t offset;
    uint64_t info;
    int64_t addend;
} mesart_elf_rela;

static int page_up(uint64_t value, uint64_t *out_value)
{
    uint64_t rounded;

    if (!out_value || value > UINT64_MAX - (MESART_ELF_PAGE_BYTES - 1u))
        return -1;
    rounded = (value + MESART_ELF_PAGE_BYTES - 1u) &
              ~(MESART_ELF_PAGE_BYTES - 1u);
    *out_value = rounded;
    return 0;
}

static void clear_bytes(void *memory, uint64_t bytes)
{
    uint8_t *out = (uint8_t *)memory;
    for (uint64_t i = 0u; i < bytes; ++i)
        out[i] = 0u;
}

static void copy_bytes(void *destination, const void *source, uint64_t bytes)
{
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (uint64_t i = 0u; i < bytes; ++i)
        out[i] = in[i];
}

static uint32_t hashes_match(const uint8_t left[MESART_SHA256_BYTES],
                             const uint8_t right[MESART_SHA256_BYTES])
{
    uint8_t difference = 0u;
    for (uint32_t i = 0u; i < MESART_SHA256_BYTES; ++i)
        difference |= (uint8_t)(left[i] ^ right[i]);
    return difference == 0u;
}

static int join_renderer_path(char out[MESART_ELF_PATH_MAX],
                              const char *bundle_root,
                              const char relative[MESART_MANIFEST_PATH_BYTES])
{
    uint32_t at = 0u;
    uint32_t relative_at = 0u;

    if (!out || !bundle_root || !relative || !relative[0] ||
        relative[0] == '/' || relative[0] == '.')
        return -1;
    while (bundle_root[at])
    {
        if (at + 1u >= MESART_ELF_PATH_MAX)
            return -2;
        out[at] = bundle_root[at];
        ++at;
    }
    if (!at)
        return -3;
    if (out[at - 1u] != '/')
        out[at++] = '/';
    while (relative_at < MESART_MANIFEST_PATH_BYTES && relative[relative_at])
    {
        char c = relative[relative_at];
        if (at + 1u >= MESART_ELF_PATH_MAX || c == '\\' || c == ':' ||
            (unsigned char)c < 0x20u)
            return -4;
        out[at++] = c;
        ++relative_at;
    }
    if (relative_at == MESART_MANIFEST_PATH_BYTES)
        return -5;
    out[at] = '\0';
    return 0;
}

static int range_in_file(uint64_t offset, uint64_t bytes, uint64_t file_bytes)
{
    return offset <= file_bytes && bytes <= file_bytes - offset;
}

static int range_in_image(uint64_t address, uint64_t bytes,
                          uint64_t low_page, uint64_t high_page)
{
    return address >= low_page && address <= high_page &&
           bytes <= high_page - address;
}

static int range_in_load_segment(const uint8_t *file_memory,
                                 const mesart_elf_header *header,
                                 uint64_t address, uint64_t bytes)
{
    for (uint32_t i = 0u; i < header->program_header_count; ++i)
    {
        const mesart_elf_program_header *program =
            (const mesart_elf_program_header *)(file_memory +
                header->program_headers_offset +
                (uint64_t)i * sizeof(*program));
        if (program->type == ELF_PT_LOAD &&
            address >= program->virtual_address)
        {
            uint64_t offset = address - program->virtual_address;
            if (offset <= program->memory_bytes &&
                bytes <= program->memory_bytes - offset)
                return 1;
        }
    }
    return 0;
}

static int valid_header(const mesart_elf_header *header, uint64_t file_bytes)
{
    uint64_t table_bytes;

    if (!header || file_bytes < sizeof(*header) ||
        header->ident[0] != 0x7fu || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != 2u || header->ident[5] != 1u ||
        header->ident[6] != 1u || header->type != ELF_ET_DYN ||
        header->machine != ELF_EM_AARCH64 || header->version != 1u ||
        header->header_bytes != sizeof(*header) ||
        header->program_header_bytes != sizeof(mesart_elf_program_header) ||
        !header->program_header_count ||
        header->program_header_count > MESART_ELF_MAX_PROGRAM_HEADERS)
        return -1;
    table_bytes = (uint64_t)header->program_header_count *
                  sizeof(mesart_elf_program_header);
    return range_in_file(header->program_headers_offset, table_bytes,
                         file_bytes) ? 0 : -2;
}

static int image_layout(const uint8_t *file_memory, uint64_t file_bytes,
                        const mesart_elf_header *header,
                        uint64_t *out_low_page, uint64_t *out_high_page)
{
    uint64_t low = UINT64_MAX;
    uint64_t high = 0u;
    uint32_t loads = 0u;

    for (uint32_t i = 0u; i < header->program_header_count; ++i)
    {
        const mesart_elf_program_header *program =
            (const mesart_elf_program_header *)(file_memory +
                header->program_headers_offset +
                (uint64_t)i * sizeof(*program));
        uint64_t end;
        uint64_t rounded_end;

        if (program->type == ELF_PT_INTERP || program->type == ELF_PT_TLS)
            return -1;
        if (program->type != ELF_PT_LOAD)
            continue;
        if (!program->memory_bytes || program->file_bytes > program->memory_bytes ||
            !range_in_file(program->offset, program->file_bytes, file_bytes) ||
            (program->virtual_address & (MESART_ELF_PAGE_BYTES - 1u)) ||
            (program->flags & ~(ELF_PF_X | ELF_PF_W | 4u)) ||
            program->virtual_address > UINT64_MAX - program->memory_bytes)
            return -2;
        if ((program->flags & (ELF_PF_X | ELF_PF_W)) == (ELF_PF_X | ELF_PF_W) ||
            page_up(program->virtual_address + program->memory_bytes,
                    &rounded_end) != 0)
            return -3;
        for (uint32_t previous = 0u; previous < i; ++previous)
        {
            const mesart_elf_program_header *older =
                (const mesart_elf_program_header *)(file_memory +
                    header->program_headers_offset +
                    (uint64_t)previous * sizeof(*older));
            uint64_t older_end;

            if (older->type != ELF_PT_LOAD ||
                page_up(older->virtual_address + older->memory_bytes,
                        &older_end) != 0)
                continue;
            if (program->virtual_address < older_end &&
                older->virtual_address < rounded_end)
                return -4;
        }
        end = program->virtual_address + program->memory_bytes;
        if (program->virtual_address < low)
            low = program->virtual_address;
        if (rounded_end > high)
            high = rounded_end;
        (void)end;
        ++loads;
    }
    if (!loads || low == UINT64_MAX || low >= high)
        return -5;
    *out_low_page = low;
    *out_high_page = high;
    return 0;
}

static int load_segments(const uint8_t *file_memory,
                         const mesart_elf_header *header,
                         uint8_t *image_memory, uint64_t low_page)
{
    for (uint32_t i = 0u; i < header->program_header_count; ++i)
    {
        const mesart_elf_program_header *program =
            (const mesart_elf_program_header *)(file_memory +
                header->program_headers_offset +
                (uint64_t)i * sizeof(*program));
        if (program->type == ELF_PT_LOAD)
            copy_bytes(image_memory + (program->virtual_address - low_page),
                       file_memory + program->offset, program->file_bytes);
    }
    return 0;
}

static int apply_relative_relocations(const uint8_t *file_memory,
                                      const mesart_elf_header *header,
                                      uint8_t *image_memory,
                                      uint64_t file_bytes,
                                      uint64_t low_page, uint64_t high_page,
                                      uint64_t load_bias)
{
    uint32_t dynamic_count = 0u;

    for (uint32_t i = 0u; i < header->program_header_count; ++i)
    {
        const mesart_elf_program_header *program =
            (const mesart_elf_program_header *)(file_memory +
                header->program_headers_offset +
                (uint64_t)i * sizeof(*program));
        uint64_t rela_address = 0u;
        uint64_t rela_bytes = 0u;
        uint64_t rela_entry_bytes = 0u;
        uint32_t terminated = 0u;

        if (program->type != ELF_PT_DYNAMIC)
            continue;
        if (++dynamic_count != 1u || !program->file_bytes ||
            !range_in_file(program->offset, program->file_bytes, file_bytes))
            return -1;
        for (uint64_t at = 0u;
             at + sizeof(mesart_elf_dynamic) <= program->file_bytes;
             at += sizeof(mesart_elf_dynamic))
        {
            const mesart_elf_dynamic *dynamic =
                (const mesart_elf_dynamic *)(file_memory + program->offset + at);
            if (dynamic->tag == ELF_DT_NULL)
            {
                terminated = 1u;
                break;
            }
            if (dynamic->tag == ELF_DT_NEEDED || dynamic->tag == ELF_DT_REL ||
                dynamic->tag == ELF_DT_RELSZ || dynamic->tag == ELF_DT_JMPREL ||
                dynamic->tag == ELF_DT_PLTRELSZ || dynamic->tag == ELF_DT_RELR ||
                dynamic->tag == ELF_DT_RELRSZ)
                return -2;
            if (dynamic->tag == ELF_DT_RELA)
                rela_address = dynamic->value;
            else if (dynamic->tag == ELF_DT_RELASZ)
                rela_bytes = dynamic->value;
            else if (dynamic->tag == ELF_DT_RELAENT)
                rela_entry_bytes = dynamic->value;
        }
        if (!terminated)
            return -3;
        if (!rela_address && !rela_bytes && !rela_entry_bytes)
            continue;
        if (!rela_address || !rela_bytes ||
            rela_entry_bytes != sizeof(mesart_elf_rela) ||
            rela_bytes % sizeof(mesart_elf_rela) ||
            !range_in_image(rela_address, rela_bytes, low_page, high_page) ||
            !range_in_load_segment(file_memory, header, rela_address,
                                   rela_bytes))
            return -4;
        for (uint64_t at = 0u; at < rela_bytes; at += sizeof(mesart_elf_rela))
        {
            const mesart_elf_rela *rela =
                (const mesart_elf_rela *)(image_memory +
                    (rela_address - low_page) + at);
            uint64_t target;
            uint64_t type = rela->info & 0xffffffffu;

            if ((rela->info >> 32u) != 0u || type != ELF_R_AARCH64_RELATIVE ||
                (rela->offset & 7u) || !range_in_image(rela->offset, 8u,
                                                        low_page, high_page) ||
                !range_in_load_segment(file_memory, header, rela->offset, 8u) ||
                rela->addend < 0 || (uint64_t)rela->addend < low_page ||
                (uint64_t)rela->addend >= high_page ||
                !range_in_load_segment(file_memory, header,
                                       (uint64_t)rela->addend, 1u))
                return -5;
            if (load_bias > UINT64_MAX - (uint64_t)rela->addend)
                return -6;
            target = load_bias + (uint64_t)rela->addend;
            copy_bytes(image_memory + (rela->offset - low_page), &target,
                       sizeof(target));
        }
    }
    return 0;
}

static int entry_is_executable(const uint8_t *file_memory,
                               const mesart_elf_header *header,
                               uint64_t entry)
{
    for (uint32_t i = 0u; i < header->program_header_count; ++i)
    {
        const mesart_elf_program_header *program =
            (const mesart_elf_program_header *)(file_memory +
                header->program_headers_offset +
                (uint64_t)i * sizeof(*program));
        if (program->type == ELF_PT_LOAD && (program->flags & ELF_PF_X) &&
            entry >= program->virtual_address &&
            entry - program->virtual_address < program->memory_bytes)
            return 1;
    }
    return 0;
}

static int map_segments(const uint8_t *file_memory,
                        const mesart_elf_header *header,
                        aarch64_user_vm *vm, uint64_t low_page,
                        uint64_t physical_base, uint64_t load_bias)
{
    for (uint32_t i = 0u; i < header->program_header_count; ++i)
    {
        const mesart_elf_program_header *program =
            (const mesart_elf_program_header *)(file_memory +
                header->program_headers_offset +
                (uint64_t)i * sizeof(*program));
        uint64_t mapped_bytes;
        uint32_t permissions = AARCH64_USER_VM_READ;

        if (program->type != ELF_PT_LOAD)
            continue;
        if (program->flags & ELF_PF_W)
            permissions |= AARCH64_USER_VM_WRITE;
        if (program->flags & ELF_PF_X)
            permissions |= AARCH64_USER_VM_EXECUTE;
        if (page_up(program->memory_bytes, &mapped_bytes) != 0 ||
            aarch64_user_vm_map(vm, load_bias + program->virtual_address,
                                physical_base +
                                    (program->virtual_address - low_page),
                                mapped_bytes, permissions) != 0)
            return -1;
    }
    return 0;
}

void mesart_elf_release_image(mesart_loaded_image *image)
{
    if (!image)
        return;
    if (image->memory && image->pages)
        pmem_free_pages(image->memory, image->pages);
    *image = (mesart_loaded_image){0};
}

int mesart_elf_load_verified(const char *bundle_root,
                             const mesart_manifest_file *renderer_file,
                             aarch64_user_vm *vm,
                             uint64_t image_base_va,
                             mesart_loaded_image *out_image)
{
#if !defined(__aarch64__) && !defined(__arm64__) && !defined(_M_ARM64)
    (void)bundle_root;
    (void)renderer_file;
    (void)vm;
    (void)image_base_va;
    (void)out_image;
    return -100;
#else
    KFile file;
    char path[MESART_ELF_PATH_MAX];
    uint8_t digest[MESART_SHA256_BYTES];
    br_sha256_context hash;
    uint8_t *file_memory = 0;
    uint8_t *image_memory = 0;
    uint64_t file_pages = 0u;
    uint64_t image_pages = 0u;
    uint64_t file_bytes;
    uint64_t read = 0u;
    uint64_t low_page;
    uint64_t high_page;
    uint64_t image_bytes;
    uint64_t physical_base;
    uint64_t load_bias;
    const mesart_elf_header *header;
    uint32_t file_open = 0u;
    int rc = -1;

    if (!bundle_root || !renderer_file || !vm || !out_image ||
        renderer_file->role != MESART_ROLE_RENDERER_SERVICE ||
        !renderer_file->bytes || renderer_file->bytes > MESART_ELF_MAX_FILE_BYTES ||
        join_renderer_path(path, bundle_root, renderer_file->path) != 0)
        return -1;
    *out_image = (mesart_loaded_image){0};
    if (kfile_open(&file, path, KFILE_READ) != 0)
        return -2;
    file_open = 1u;
    file_bytes = kfile_size(&file);
    if (file_bytes != renderer_file->bytes ||
        page_up(file_bytes, &file_pages) != 0 ||
        !(file_memory = (uint8_t *)pmem_alloc_pages(file_pages)))
    {
        kfile_close(&file);
        file_open = 0u;
        return -3;
    }
    clear_bytes(file_memory, file_pages * MESART_ELF_PAGE_BYTES);
    br_sha256_init(&hash);
    while (read < file_bytes)
    {
        uint32_t got = 0u;
        uint32_t want = file_bytes - read > 4096u ? 4096u :
                                                      (uint32_t)(file_bytes - read);
        if (kfile_read(&file, file_memory + read, want, &got) != 0 || got != want)
        {
            rc = -4;
            goto done;
        }
        br_sha256_update(&hash, file_memory + read, got);
        read += got;
    }
    kfile_close(&file);
    file_open = 0u;
    br_sha256_out(&hash, digest);
    if (!hashes_match(digest, renderer_file->sha256))
    {
        rc = -5;
        goto done;
    }
    header = (const mesart_elf_header *)file_memory;
    if (valid_header(header, file_bytes) != 0 ||
        image_layout(file_memory, file_bytes, header, &low_page, &high_page) != 0 ||
        (image_bytes = high_page - low_page) == 0u ||
        image_bytes > MESART_ELF_MAX_FILE_BYTES)
    {
        rc = -6;
        goto done;
    }
    if (!image_base_va)
        image_base_va = MESART_ELF_DEFAULT_BASE_VA;
    if ((image_base_va & (MESART_ELF_PAGE_BYTES - 1u)) ||
        image_base_va > UINT64_MAX - image_bytes ||
        page_up(image_bytes, &image_bytes) != 0 ||
        !(image_memory = (uint8_t *)pmem_alloc_pages(image_bytes / MESART_ELF_PAGE_BYTES)))
    {
        rc = -7;
        goto done;
    }
    image_pages = image_bytes / MESART_ELF_PAGE_BYTES;
    clear_bytes(image_memory, image_bytes);
    if (aarch64_user_vm_translate_current((uint64_t)(uintptr_t)image_memory,
                                          &physical_base) != 0 ||
        (physical_base & (MESART_ELF_PAGE_BYTES - 1u)) ||
        image_base_va < low_page)
    {
        rc = -8;
        goto done;
    }
    load_bias = image_base_va - low_page;
    load_segments(file_memory, header, image_memory, low_page);
    if (apply_relative_relocations(file_memory, header, image_memory, file_bytes,
                                   low_page, high_page, load_bias) != 0 ||
        (header->entry & 3u) ||
        !range_in_image(header->entry, 4u, low_page, high_page) ||
        !entry_is_executable(file_memory, header, header->entry) ||
        map_segments(file_memory, header, vm, low_page, physical_base,
                     load_bias) != 0)
    {
        rc = -9;
        goto done;
    }
    asm_sync_executable_range(image_memory, image_bytes);
    *out_image = (mesart_loaded_image){image_memory, image_pages, physical_base,
                                       image_base_va, image_bytes,
                                       load_bias + header->entry};
    image_memory = 0;
    image_pages = 0u;
    rc = 0;
done:
    if (file_open)
        kfile_close(&file);
    if (image_memory && image_pages)
        pmem_free_pages(image_memory, image_pages);
    if (file_memory && file_pages)
        pmem_free_pages(file_memory, file_pages);
    return rc;
#endif
}
