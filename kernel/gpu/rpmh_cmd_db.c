#include "gpu/rpmh_cmd_db.h"

#define RPMH_CMD_DB_HEADER_SIZE 144u
#define RPMH_CMD_DB_RSC_COUNT 8u
#define RPMH_CMD_DB_RSC_SIZE 16u
#define RPMH_CMD_DB_ENTRY_SIZE 24u
#define RPMH_CMD_DB_MAGIC_WORD 0x0c0330dbu

#define EFI_BOOT_SERVICES_CODE 3u
#define EFI_BOOT_SERVICES_DATA 4u
#define EFI_CONVENTIONAL_MEMORY 7u

typedef struct rpmh_efi_memory_descriptor
{
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t page_count;
    uint64_t attributes;
} rpmh_efi_memory_descriptor;

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int range_valid(uint32_t whole, uint32_t offset, uint32_t size)
{
    return offset <= whole && size <= whole - offset;
}

static int resource_id_equal(const uint8_t entry_id[8], const char *id)
{
    uint32_t i;

    if (!id)
        return 0;
    for (i = 0u; i < 8u; ++i)
    {
        uint8_t expected = (uint8_t)id[i];
        if (entry_id[i] != expected)
            return 0;
        if (!expected)
            return 1;
    }
    return id[8] == '\0';
}

int rpmh_cmd_db_open(rpmh_cmd_db *db, const void *bytes, uint32_t size_bytes)
{
    const uint8_t *data = (const uint8_t *)bytes;

    if (!db || !data || size_bytes < RPMH_CMD_DB_HEADER_SIZE)
        return -1;
    /* The magic is stored directly after the 32-bit version. */
    if (data[4] != 0xdbu || data[5] != 0x30u ||
        data[6] != 0x03u || data[7] != 0x0cu)
        return -2;
    db->bytes = data;
    db->size_bytes = size_bytes;
    db->data_offset = RPMH_CMD_DB_HEADER_SIZE;
    return 0;
}

int rpmh_cmd_db_find(const rpmh_cmd_db *db, const char *resource_id,
                     rpmh_cmd_db_resource *out)
{
    if (!db || !db->bytes || !out || !resource_id ||
        db->data_offset > db->size_bytes)
        return -1;

    for (uint32_t rsc = 0u; rsc < RPMH_CMD_DB_RSC_COUNT; ++rsc)
    {
        const uint8_t *header = db->bytes + 8u + rsc * RPMH_CMD_DB_RSC_SIZE;
        uint16_t slave_id = read_le16(header);
        uint16_t header_offset = read_le16(header + 2u);
        uint16_t data_offset = read_le16(header + 4u);
        uint16_t count = read_le16(header + 6u);
        uint32_t entries_offset = db->data_offset + header_offset;

        if (!slave_id)
            continue;
        if (!range_valid(db->size_bytes, entries_offset,
                         (uint32_t)count * RPMH_CMD_DB_ENTRY_SIZE))
            return -2;
        for (uint32_t entry_index = 0u; entry_index < count; ++entry_index)
        {
            const uint8_t *entry = db->bytes + entries_offset +
                                   entry_index * RPMH_CMD_DB_ENTRY_SIZE;
            uint16_t aux_size;
            uint16_t aux_offset;
            uint32_t aux_start;

            if (!resource_id_equal(entry, resource_id))
                continue;
            aux_size = read_le16(entry + 20u);
            aux_offset = read_le16(entry + 22u);
            aux_start = db->data_offset + data_offset + aux_offset;
            if (!range_valid(db->size_bytes, aux_start, aux_size))
                return -3;
            *out = (rpmh_cmd_db_resource){
                read_le32(entry + 16u), slave_id,
                db->bytes + aux_start, aux_size};
            return 0;
        }
    }
    return -4;
}

static int firmware_range_is_reserved(const boot_info *boot, uint64_t base,
                                      uint32_t size_bytes)
{
    const uint8_t *at;
    const uint8_t *end;
    uint64_t descriptor_size;
    uint64_t range_end;

    if (!boot || !boot->mmap || !boot->mmap_size || !size_bytes)
        return 0;
    range_end = base + size_bytes;
    if (range_end < base)
        return 0;
    descriptor_size = boot->mmap_desc_size ? boot->mmap_desc_size :
                                             sizeof(rpmh_efi_memory_descriptor);
    if (descriptor_size < sizeof(rpmh_efi_memory_descriptor))
        return 0;
    at = (const uint8_t *)(uintptr_t)boot->mmap;
    end = at + boot->mmap_size;
    for (; at + descriptor_size <= end; at += descriptor_size)
    {
        const rpmh_efi_memory_descriptor *descriptor =
            (const rpmh_efi_memory_descriptor *)(const void *)at;
        uint64_t descriptor_end = descriptor->physical_start +
                                  descriptor->page_count * 4096u;

        if (descriptor_end < descriptor->physical_start ||
            base < descriptor->physical_start || range_end > descriptor_end)
            continue;
        return descriptor->type != EFI_BOOT_SERVICES_CODE &&
               descriptor->type != EFI_BOOT_SERVICES_DATA &&
               descriptor->type != EFI_CONVENTIONAL_MEMORY;
    }
    return 0;
}

int rpmh_cmd_db_probe_firmware(const boot_info *boot, uint64_t physical_base,
                               uint32_t size_bytes,
                               rpmh_cmd_db_mapping *out)
{
    gpu_mmio_window window = {0};
    uint32_t magic;

    if (!out || !physical_base || size_bytes < RPMH_CMD_DB_HEADER_SIZE ||
        !firmware_range_is_reserved(boot, physical_base, size_bytes))
        return -1;
    if (gpu_mmio_map_window(&window, physical_base, size_bytes) != 0)
        return -2;
    if (gpu_mmio_try_read32(&window, 4u, &magic) != 0 ||
        magic != RPMH_CMD_DB_MAGIC_WORD)
    {
        gpu_mmio_unmap_window(&window);
        return -3;
    }
    *out = (rpmh_cmd_db_mapping){0};
    out->window = window;
    out->physical_base = physical_base;
    out->size_bytes = size_bytes;
    if (rpmh_cmd_db_open(&out->db, (const void *)window.cpu_base,
                         size_bytes) != 0)
    {
        rpmh_cmd_db_mapping_release(out);
        return -4;
    }
    return 0;
}

void rpmh_cmd_db_mapping_release(rpmh_cmd_db_mapping *mapping)
{
    if (!mapping)
        return;
    gpu_mmio_unmap_window(&mapping->window);
    *mapping = (rpmh_cmd_db_mapping){0};
}
