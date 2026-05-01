#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SACX_MAGIC 0x58434153u /* "SACX" */
#define SACX_VERSION 1u
#define SACX_FAT_VERSION 2u

#define SACX_ARCH_UNKNOWN 0u
#define SACX_ARCH_AA64 0x0000AA64u
#define SACX_ARCH_X64 0x00008664u

#define SACX_SLICE_KIND_NATIVE 1u

    enum
    {
        SACX_RELOC_RELATIVE64 = 1u
    };

#pragma pack(push, 1)
    typedef struct sacx_header
    {
        uint32_t magic;
        uint16_t version;
        uint16_t header_size;
        uint32_t flags;
        uint32_t crc32; /* checksum over full file with this field set to 0 */

        uint32_t entry_rva;
        uint32_t image_size;

        uint32_t segment_offset;
        uint32_t segment_count;

        uint32_t reloc_offset;
        uint32_t reloc_count;

        uint32_t import_offset;
        uint32_t import_count;

        uint32_t strings_offset;
        uint32_t strings_size;

        uint32_t image_offset;
        uint32_t reserved0;
    } sacx_header;

    typedef struct sacx_fat_header
    {
        uint32_t magic;
        uint16_t version;
        uint16_t header_size;
        uint32_t flags;
        uint32_t crc32; /* checksum over full container with this field set to 0 */

        uint32_t slice_offset;
        uint32_t slice_count;

        uint32_t reserved0;
        uint32_t reserved1;
    } sacx_fat_header;

    typedef struct sacx_slice
    {
        uint32_t arch;
        uint32_t kind;
        uint32_t file_offset;
        uint32_t file_size;
        uint32_t flags;
        uint32_t reserved0;
    } sacx_slice;

    typedef struct sacx_segment
    {
        uint32_t rva;
        uint32_t file_offset; /* offset into image blob at header.image_offset */
        uint32_t file_size;
        uint32_t mem_size;
        uint32_t flags;
    } sacx_segment;

    typedef struct sacx_reloc
    {
        uint32_t type;
        uint32_t target_rva;
        uint64_t addend;
    } sacx_reloc;

    typedef struct sacx_import
    {
        uint32_t name_offset; /* offset into header.strings_offset region */
        uint32_t reserved0;
    } sacx_import;
#pragma pack(pop)

#ifdef __cplusplus
}
#endif
