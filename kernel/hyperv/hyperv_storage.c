#include "hyperv/hyperv_storage.h"
#include "hyperv/hyperv.h"
#include "hyperv/vmbus.h"
#include "memory/pmem.h"
#include "terminal/terminal_api.h"

typedef struct
{
    uint64_t block_count;
    uint64_t lba_base;
    uint32_t block_size;
    uint8_t target;
    uint8_t lun;
    uint8_t *bounce;
} hyperv_storage_ctx;

static hyperv_storage_ctx G_hyperv_storage;

static int hyperv_storage_read(void *ctx, uint64_t lba,
                               uint32_t count, void *buffer)
{
    hyperv_storage_ctx *storage = (hyperv_storage_ctx *)ctx;
    uint8_t *destination = (uint8_t *)buffer;

    if (!storage || !storage->bounce || !destination || !count ||
        storage->block_size != 512u ||
        lba >= storage->block_count ||
        count > storage->block_count - lba)
        return -1;

    lba += storage->lba_base;
    while (count)
    {
        uint32_t chunk = count > 8u ? 8u : count;
        uint32_t bytes = chunk * storage->block_size;
        int read_ok = 0;

        if (lba > 0xFFFFFFFFull)
            return -1;
        for (uint32_t attempt = 0u; attempt < 4u; ++attempt)
        {
            if (vmbus_storvsc_read10(storage->target, storage->lun,
                                     (uint32_t)lba, (uint16_t)chunk,
                                     storage->bounce, bytes) == 0)
            {
                read_ok = 1;
                break;
            }
        }
        if (!read_ok)
        {
            terminal_error("storvsc: read failed lba=");
            terminal_print_inline_hex64(lba);
            terminal_print(" blocks=");
            terminal_print_inline_hex32(chunk);
            return -1;
        }
        for (uint32_t i = 0; i < bytes; ++i)
            destination[i] = storage->bounce[i];

        destination += bytes;
        lba += chunk;
        count -= chunk;
    }
    return 0;
}

static int hyperv_storage_write(void *ctx, uint64_t lba,
                                uint32_t count, const void *buffer)
{
    hyperv_storage_ctx *storage = (hyperv_storage_ctx *)ctx;
    const uint8_t *source = (const uint8_t *)buffer;

    if (!storage || !storage->bounce || !source || !count ||
        storage->block_size != 512u ||
        lba >= storage->block_count ||
        count > storage->block_count - lba)
        return -1;

    lba += storage->lba_base;
    while (count)
    {
        uint32_t chunk = count > 8u ? 8u : count;
        uint32_t bytes = chunk * storage->block_size;
        int write_ok = 0;

        if (lba > 0xFFFFFFFFull)
            return -1;

        for (uint32_t i = 0; i < bytes; ++i)
            storage->bounce[i] = source[i];

        for (uint32_t attempt = 0u; attempt < 4u; ++attempt)
        {
            if (vmbus_storvsc_write10(storage->target, storage->lun,
                                      (uint32_t)lba, (uint16_t)chunk,
                                      storage->bounce, bytes) == 0)
            {
                write_ok = 1;
                break;
            }
        }
        if (!write_ok)
        {
            terminal_error("storvsc: write failed lba=");
            terminal_print_inline_hex64(lba);
            terminal_print(" blocks=");
            terminal_print_inline_hex32(chunk);
            return -1;
        }

        source += bytes;
        lba += chunk;
        count -= chunk;
    }
    return 0;
}

static uint32_t hyperv_storage_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t hyperv_storage_le64(const uint8_t *p)
{
    return (uint64_t)hyperv_storage_le32(p) |
           ((uint64_t)hyperv_storage_le32(p + 4) << 32);
}

static int hyperv_storage_is_fat_vbr(const uint8_t sector[512])
{
    if ((sector[0] != 0xEBu && sector[0] != 0xE9u) ||
        sector[11] != 0x00u || sector[12] != 0x02u ||
        sector[510] != 0x55u || sector[511] != 0xAAu)
        return 0;
    return 1;
}

static uint64_t hyperv_storage_find_gpt_volume(uint8_t *scratch,
                                               uint64_t *volume_blocks)
{
    uint64_t entries_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint64_t loaded_lba = ~0ull;

    if (!scratch ||
        vmbus_storvsc_read10(0u, 0u, 1u, 1u, scratch, 512u) != 0)
        return 0u;
    if (scratch[0] != 'E' || scratch[1] != 'F' ||
        scratch[2] != 'I' || scratch[3] != ' ' ||
        scratch[4] != 'P' || scratch[5] != 'A' ||
        scratch[6] != 'R' || scratch[7] != 'T')
    {
        terminal_error("storvsc: GPT header signature invalid");
        return 0u;
    }

    entries_lba = hyperv_storage_le64(scratch + 72);
    entry_count = hyperv_storage_le32(scratch + 80);
    entry_size = hyperv_storage_le32(scratch + 84);
    terminal_print("storvsc: GPT entries_lba=");
    terminal_print_inline_hex64(entries_lba);
    terminal_print(" count=");
    terminal_print_inline_hex32(entry_count);
    terminal_print(" entry_size=");
    terminal_print_inline_hex32(entry_size);
    if (!entries_lba || !entry_count ||
        entry_size < 128u || entry_size > 512u ||
        (512u % entry_size) != 0u)
        return 0u;
    if (entry_count > 128u)
        entry_count = 128u;

    for (uint32_t index = 0; index < entry_count; ++index)
    {
        uint64_t table_lba =
            entries_lba + ((uint64_t)index * entry_size) / 512u;
        uint32_t offset = (index * entry_size) % 512u;
        const uint8_t *entry;
        uint64_t first_lba;
        uint64_t last_lba;
        int empty = 1;

        if (table_lba != loaded_lba)
        {
            if (table_lba > 0xFFFFFFFFull ||
                vmbus_storvsc_read10(0u, 0u, (uint32_t)table_lba,
                                     1u, scratch, 512u) != 0)
                return 0u;
            loaded_lba = table_lba;
        }
        entry = scratch + offset;
        for (uint32_t i = 0; i < 16u; ++i)
        {
            if (entry[i] != 0u)
            {
                empty = 0;
                break;
            }
        }
        if (empty)
            continue;

        first_lba = hyperv_storage_le64(entry + 32);
        last_lba = hyperv_storage_le64(entry + 40);
        terminal_print("storvsc: GPT partition index=");
        terminal_print_inline_hex32(index);
        terminal_print(" first_lba=");
        terminal_print_inline_hex64(first_lba);
        terminal_print(" last_lba=");
        terminal_print_inline_hex64(last_lba);
        if (!first_lba || last_lba < first_lba ||
            first_lba > 0xFFFFFFFFull)
            continue;

        if (vmbus_storvsc_read10(0u, 0u, (uint32_t)first_lba,
                                 1u, scratch, 512u) != 0)
            return 0u;
        loaded_lba = ~0ull;
        if (hyperv_storage_is_fat_vbr(scratch))
        {
            if (volume_blocks)
                *volume_blocks = last_lba - first_lba + 1u;
            return first_lba;
        }
    }
    return 0u;
}

static uint64_t hyperv_storage_find_mbr_volume(const uint8_t sector0[512],
                                               uint64_t *volume_blocks)
{
    uint64_t selected = 0u;

    if (volume_blocks)
        *volume_blocks = 0u;
    if (sector0[510] != 0x55u || sector0[511] != 0xAAu)
        return 0u;
    if (sector0[0] == 0xEBu || sector0[0] == 0xE9u)
        return 0u;

    for (uint32_t index = 0; index < 4u; ++index)
    {
        const uint8_t *entry = sector0 + 446u + index * 16u;
        uint8_t type = entry[4];
        uint32_t first_lba = hyperv_storage_le32(entry + 8);
        uint32_t blocks = hyperv_storage_le32(entry + 12);

        terminal_print("storvsc: MBR partition index=");
        terminal_print_inline_hex32(index);
        terminal_print(" type=");
        terminal_print_inline_hex32(type);
        terminal_print(" first_lba=");
        terminal_print_inline_hex32(first_lba);
        terminal_print(" blocks=");
        terminal_print_inline_hex32(blocks);

        if (!selected && first_lba && blocks &&
            (type == 0x0Bu || type == 0x0Cu ||
             type == 0x06u || type == 0x0Eu ||
             type == 0xEFu))
        {
            selected = first_lba;
            if (volume_blocks)
                *volume_blocks = blocks;
        }
    }
    return selected;
}

int hyperv_storage_try_bind(blockdev_t *out, uint64_t acpi_rsdp)
{
    uint16_t protocol_version = 0u;
    uint16_t max_channels = 0u;
    uint32_t channel_flags = 0u;
    uint32_t max_transfer_bytes = 0u;
    uint64_t block_count = 0u;
    uint32_t block_size = 0u;
    uint8_t *sector0 = 0;
    uint64_t volume_blocks = 0u;
    uint64_t volume_lba = 0u;

    if (out)
        *out = (blockdev_t){0};

    if (!hyperv_core_ready())
    {
        terminal_error("storvsc: Hyper-V hypercall transport unavailable");
        return -1;
    }

    terminal_print("storvsc: Hyper-V hypercall transport verified");
    if (vmbus_probe_contact(acpi_rsdp) != 0)
    {
        terminal_error("storvsc: VMBus contact failed");
        return -1;
    }

    terminal_print("storvsc: VMBus contact established");
    if (vmbus_find_storvsc_offer() != 0)
    {
        terminal_error("storvsc: storage channel offer missing");
        return -1;
    }

    terminal_print("storvsc: storage channel offer bound relid=");
    terminal_print_inline_hex32(vmbus_storvsc_relid());
    if (vmbus_open_storvsc_channel() != 0)
    {
        terminal_error("storvsc: storage channel open failed");
        return -1;
    }

    terminal_print("storvsc: VMBus storage channel open");
    if (vmbus_storvsc_begin_initialization() != 0)
    {
        terminal_error("storvsc: VSTOR BEGIN_INITIALIZATION failed");
        return -1;
    }

    terminal_print("storvsc: VSTOR initialization handshake started");
    if (vmbus_storvsc_negotiate_protocol(&protocol_version) != 0)
    {
        terminal_error("storvsc: VSTOR protocol negotiation failed");
        return -1;
    }

    terminal_print("storvsc: VSTOR protocol negotiation complete version=");
    terminal_print_inline_hex32(protocol_version);
    if (vmbus_storvsc_query_properties(&max_channels,
                                       &channel_flags,
                                       &max_transfer_bytes) != 0)
    {
        terminal_error("storvsc: storage property query failed");
        return -1;
    }

    terminal_print("storvsc: storage channel properties accepted");
    if (vmbus_storvsc_end_initialization() != 0)
    {
        terminal_error("storvsc: VSTOR END_INITIALIZATION failed");
        return -1;
    }

    terminal_print("storvsc: VSTOR channel initialization complete");
    if (vmbus_storvsc_test_unit_ready(0u, 0u) != 0)
    {
        terminal_error("storvsc: SCSI target 0 LUN 0 not ready");
        return -1;
    }

    terminal_print("storvsc: SCSI target 0 LUN 0 is ready");
    if (vmbus_storvsc_inquiry(0u, 0u) != 0)
    {
        terminal_error("storvsc: SCSI INQUIRY failed");
        return -1;
    }

    terminal_print("storvsc: DMA-backed SCSI INQUIRY succeeded");
    if (vmbus_storvsc_read_capacity(0u, 0u,
                                    &block_count, &block_size) != 0)
    {
        terminal_error("storvsc: SCSI READ CAPACITY failed");
        return -1;
    }

    terminal_print("storvsc: disk capacity discovered");
    if (block_size != 512u)
    {
        terminal_error("storvsc: unsupported logical block size");
        return -1;
    }
    sector0 = (uint8_t *)pmem_alloc_pages(1);
    if (!sector0 ||
        vmbus_storvsc_read10(0u, 0u, 0u, 1u,
                             sector0, block_size) != 0)
    {
        terminal_error("storvsc: sector zero READ_10 failed");
        return -1;
    }

    terminal_print("storvsc: sector0 first_dword=");
    terminal_print_inline_hex32((uint32_t)sector0[0] |
                                ((uint32_t)sector0[1] << 8) |
                                ((uint32_t)sector0[2] << 16) |
                                ((uint32_t)sector0[3] << 24));
    terminal_print("storvsc: sector0 signature=");
    terminal_print_inline_hex32((uint32_t)sector0[510] |
                                ((uint32_t)sector0[511] << 8));
    if (sector0[510] != 0x55u || sector0[511] != 0xAAu)
    {
        terminal_error("storvsc: sector zero lacks 55AA signature");
        return -1;
    }

    terminal_print("storvsc: persistent disk sector read verified");
    volume_lba = hyperv_storage_find_mbr_volume(sector0, &volume_blocks);
    if (!volume_lba)
    {
        int protective_gpt = 0;

        for (uint32_t index = 0; index < 4u; ++index)
        {
            if (sector0[446u + index * 16u + 4u] == 0xEEu)
            {
                protective_gpt = 1;
                break;
            }
        }
        if (protective_gpt)
        {
            terminal_print("storvsc: protective MBR found; scanning GPT");
            volume_lba =
                hyperv_storage_find_gpt_volume(sector0, &volume_blocks);
        }
    }
    if (!volume_lba)
    {
        terminal_warn("storvsc: no FAT partition found; using whole disk");
        volume_blocks = block_count;
    }
    else
    {
        terminal_print("storvsc: partition-relative blockdev base=");
        terminal_print_inline_hex64(volume_lba);
        terminal_print(" blocks=");
        terminal_print_inline_hex64(volume_blocks);
    }

    G_hyperv_storage.block_count = volume_blocks;
    G_hyperv_storage.lba_base = volume_lba;
    G_hyperv_storage.block_size = block_size;
    G_hyperv_storage.target = 0u;
    G_hyperv_storage.lun = 0u;
    G_hyperv_storage.bounce = sector0;

    out->ctx = &G_hyperv_storage;
    out->sector_size = block_size;
    out->read = hyperv_storage_read;
    out->write = hyperv_storage_write;
    terminal_print("storvsc: partition-relative read/write blockdev bound");
    return 0;
}
