#include "gpu/gpu_gmu_hfi.h"
#include "asm/asm.h"

#define GPU_GMU_HFI_TABLE_WORDS        6u
#define GPU_GMU_HFI_QUEUE_HEADER_WORDS 12u
#define GPU_GMU_HFI_QUEUE_WORDS        (0x1000u / sizeof(uint32_t))
#define GPU_GMU_HFI_QUEUE0_DATA_BYTES  0x1000u
#define GPU_GMU_HFI_QUEUE1_DATA_BYTES  0x2000u

/* These are direct byte offsets in the GMU MMIO resource. */
#define GPU_GMU_GMU2HOST_INTR_CLR      (0x00005191u * sizeof(uint32_t))
#define GPU_GMU_GMU2HOST_INTR_INFO     (0x00005192u * sizeof(uint32_t))
#define GPU_GMU_HOST2GMU_INTR_SET      (0x00005194u * sizeof(uint32_t))
#define GPU_GMU_MSGQ_INTERRUPT         1u

#define GPU_GMU_HFI_H2F_BW_TABLE        3u
#define GPU_GMU_HFI_H2F_PERF_TABLE      4u
#define GPU_GMU_HFI_H2F_START           10u
#define GPU_GMU_HFI_H2F_CORE_FW_START   14u
#define GPU_GMU_HFI_H2F_GX_BW_PERF_VOTE 30u

/* Keep the on-wire layout private to this transport implementation.  The
 * public tables above contain no packet header and no platform-specific
 * command database details. */
typedef struct gpu_gmu_hfi_gen7_perf_message
{
    uint32_t header;
    uint32_t gx_level_count;
    uint32_t cx_level_count;
    gpu_gmu_hfi_gen7_gx_level gx[GPU_GMU_HFI_GEN7_MAX_GX_LEVELS];
    gpu_gmu_hfi_gen7_cx_level cx[GPU_GMU_HFI_GEN7_MAX_CX_LEVELS];
} gpu_gmu_hfi_gen7_perf_message;

typedef struct gpu_gmu_hfi_gen7_bw_message
{
    uint32_t header;
    uint32_t level_count;
    uint32_t cnoc_command_count;
    uint32_t ddr_command_count;
    uint32_t cnoc_wait_mask;
    uint32_t ddr_wait_mask;
    uint32_t cnoc_addresses[GPU_GMU_HFI_GEN7_MAX_CNOC_COMMANDS];
    uint32_t cnoc_data[2u][GPU_GMU_HFI_GEN7_MAX_CNOC_COMMANDS];
    uint32_t ddr_addresses[GPU_GMU_HFI_GEN7_MAX_DDR_COMMANDS];
    uint32_t ddr_data[GPU_GMU_HFI_GEN7_MAX_BW_LEVELS]
                     [GPU_GMU_HFI_GEN7_MAX_DDR_COMMANDS];
} gpu_gmu_hfi_gen7_bw_message;

static int gen7_request(const gpu_mmio_window *gmu, gpu_gmu_memory *memory,
                        uint8_t message_id, uint32_t *message,
                        uint32_t message_dwords)
{
    if (!message || message_dwords < 1u)
        return -1;
    return gpu_gmu_hfi_request(gmu, memory, message_id, message + 1u,
                               message_dwords - 1u, 0, 0u, 0);
}

static uint32_t *queue_header(gpu_gmu_memory *memory, uint32_t queue)
{
    if (!memory || !memory->hfi.cpu || queue >= memory->hfi_queue_count)
        return 0;
    return (uint32_t *)memory->hfi.cpu + GPU_GMU_HFI_TABLE_WORDS +
           queue * GPU_GMU_HFI_QUEUE_HEADER_WORDS;
}

static uint32_t *queue_data(gpu_gmu_memory *memory, uint32_t queue)
{
    uint32_t offset;

    if (!memory || !memory->hfi.cpu)
        return 0;
    offset = queue == GPU_GMU_HFI_COMMAND_QUEUE ?
                 GPU_GMU_HFI_QUEUE0_DATA_BYTES : GPU_GMU_HFI_QUEUE1_DATA_BYTES;
    if (queue >= memory->hfi_queue_count || offset >= memory->hfi.size_bytes)
        return 0;
    return (uint32_t *)((uint8_t *)memory->hfi.cpu + offset);
}

static uint32_t queue_space(uint32_t write, uint32_t read)
{
    return read > write ? read - write - 1u :
                          GPU_GMU_HFI_QUEUE_WORDS - write + read - 1u;
}

int gpu_gmu_hfi_send(const gpu_mmio_window *gmu, gpu_gmu_memory *memory,
                     uint8_t message_id, const uint32_t *payload,
                     uint32_t payload_dwords, uint32_t *out_sequence)
{
    uint32_t *header;
    uint32_t *data;
    uint32_t write;
    uint32_t read;
    uint32_t sequence;
    uint32_t dwords;
    uint32_t padded_dwords;

    if (out_sequence)
        *out_sequence = 0u;
    if (!gmu || !memory || payload_dwords > 254u ||
        (payload_dwords && !payload))
        return -1;
    header = queue_header(memory, GPU_GMU_HFI_COMMAND_QUEUE);
    data = queue_data(memory, GPU_GMU_HFI_COMMAND_QUEUE);
    if (!header || !data || header[0] != 1u || header[3] != GPU_GMU_HFI_QUEUE_WORDS)
        return -2;

    /* GMU may have advanced read_index since the previous send. */
    asm_dma_invalidate_range(header, GPU_GMU_HFI_QUEUE_HEADER_WORDS *
                                         sizeof(uint32_t));
    write = header[11];
    read = header[10];
    dwords = payload_dwords + 1u;
    padded_dwords = (dwords + 3u) & ~3u;
    if (write >= GPU_GMU_HFI_QUEUE_WORDS || read >= GPU_GMU_HFI_QUEUE_WORDS ||
        queue_space(write, read) < padded_dwords)
        return -3;

    sequence = (memory->hfi_next_sequence + 1u) & 0xfffu;
    data[write] = (sequence << 20) | (dwords << 8) | message_id;
    write = (write + 1u) % GPU_GMU_HFI_QUEUE_WORDS;
    for (uint32_t i = 0u; i < payload_dwords; ++i)
    {
        data[write] = payload[i];
        write = (write + 1u) % GPU_GMU_HFI_QUEUE_WORDS;
    }
    /* Non-legacy GMU packets consume four-dword slots. */
    while (write & 3u)
    {
        data[write] = 0xfafafafau;
        write = (write + 1u) % GPU_GMU_HFI_QUEUE_WORDS;
    }
    header[11] = write;
    memory->hfi_next_sequence = sequence;
    gpu_buffer_prepare_for_device(&memory->hfi);
    if (gpu_mmio_try_write32(gmu, GPU_GMU_HOST2GMU_INTR_SET,
                             GPU_GMU_MSGQ_INTERRUPT) != 0)
        return -4;
    if (out_sequence)
        *out_sequence = sequence;
    return 0;
}

int gpu_gmu_hfi_read_response(const gpu_mmio_window *gmu,
                              gpu_gmu_memory *memory,
                              uint32_t *out_words, uint32_t capacity_dwords,
                              uint32_t *out_dwords)
{
    uint32_t *header;
    uint32_t *data;
    uint32_t status;
    uint32_t read;
    uint32_t write;
    uint32_t dwords;

    if (out_dwords)
        *out_dwords = 0u;
    if (!gmu || !memory || !out_words || !capacity_dwords)
        return -1;
    for (uint32_t attempt = 0u; attempt < 100000u; ++attempt)
    {
        if (gpu_mmio_try_read32(gmu, GPU_GMU_GMU2HOST_INTR_INFO, &status) != 0)
            return -2;
        if (status & GPU_GMU_MSGQ_INTERRUPT)
            break;
        if (attempt == 99999u)
            return -3;
        asm_relax();
    }
    if (gpu_mmio_try_write32(gmu, GPU_GMU_GMU2HOST_INTR_CLR,
                             GPU_GMU_MSGQ_INTERRUPT) != 0)
        return -4;
    header = queue_header(memory, GPU_GMU_HFI_RESPONSE_QUEUE);
    data = queue_data(memory, GPU_GMU_HFI_RESPONSE_QUEUE);
    if (!header || !data)
        return -5;
    asm_dma_invalidate_range(memory->hfi.cpu, memory->hfi.size_bytes);
    read = header[10];
    write = header[11];
    if (read >= GPU_GMU_HFI_QUEUE_WORDS || write >= GPU_GMU_HFI_QUEUE_WORDS ||
        read == write)
        return -6;
    dwords = (data[read] >> 8) & 0xffu;
    if (!dwords || dwords > capacity_dwords || dwords > GPU_GMU_HFI_QUEUE_WORDS)
        return -7;
    for (uint32_t i = 0u; i < dwords; ++i)
    {
        out_words[i] = data[read];
        read = (read + 1u) % GPU_GMU_HFI_QUEUE_WORDS;
    }
    while (read & 3u)
        read = (read + 1u) % GPU_GMU_HFI_QUEUE_WORDS;
    header[10] = read;
    gpu_buffer_prepare_for_device(&memory->hfi);
    if (out_dwords)
        *out_dwords = dwords;
    return 0;
}

int gpu_gmu_hfi_request(const gpu_mmio_window *gmu, gpu_gmu_memory *memory,
                        uint8_t message_id, const uint32_t *payload,
                        uint32_t payload_dwords, uint32_t *out_payload,
                        uint32_t payload_capacity_dwords,
                        uint32_t *out_payload_dwords)
{
    uint32_t response[20];
    uint32_t sequence;
    uint32_t response_dwords;

    if (out_payload_dwords)
        *out_payload_dwords = 0u;
    if (gpu_gmu_hfi_send(gmu, memory, message_id, payload, payload_dwords,
                         &sequence) != 0)
        return -1;
    /* A response packet has header, original header, error, then up to 16
     * dwords of payload.  Ignore a bounded number of stale notifications. */
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt)
    {
        if (gpu_gmu_hfi_read_response(gmu, memory, response,
                                      (uint32_t)(sizeof(response) /
                                                 sizeof(response[0])),
                                      &response_dwords) != 0)
            return -2;
        if ((response[0] & 0xffu) == 100u)
            return -3; /* firmware error packet */
        if (response_dwords < 3u || ((response[1] >> 20) & 0xfffu) != sequence)
            continue;
        if (response[2] != 0u)
            return -4;
        response_dwords -= 3u;
        if (response_dwords > payload_capacity_dwords ||
            (response_dwords && !out_payload))
            return -5;
        for (uint32_t i = 0u; i < response_dwords; ++i)
            out_payload[i] = response[i + 3u];
        if (out_payload_dwords)
            *out_payload_dwords = response_dwords;
        return 0;
    }
    return -6;
}

int gpu_gmu_hfi_gen7_send_perf_table(const gpu_mmio_window *gmu,
                                     gpu_gmu_memory *memory,
                                     const gpu_gmu_hfi_gen7_perf_table *table)
{
    gpu_gmu_hfi_gen7_perf_message message = {0};

    if (!table || !table->gx_level_count || !table->cx_level_count ||
        table->gx_level_count > GPU_GMU_HFI_GEN7_MAX_GX_LEVELS ||
        table->cx_level_count > GPU_GMU_HFI_GEN7_MAX_CX_LEVELS)
        return -1;
    message.gx_level_count = table->gx_level_count;
    message.cx_level_count = table->cx_level_count;
    for (uint32_t i = 0u; i < table->gx_level_count; ++i)
        message.gx[i] = table->gx[i];
    for (uint32_t i = 0u; i < table->cx_level_count; ++i)
        message.cx[i] = table->cx[i];
    return gen7_request(gmu, memory, GPU_GMU_HFI_H2F_PERF_TABLE,
                        (uint32_t *)(void *)&message,
                        (uint32_t)(sizeof(message) / sizeof(uint32_t)));
}

int gpu_gmu_hfi_gen7_send_bw_table(const gpu_mmio_window *gmu,
                                   gpu_gmu_memory *memory,
                                   const gpu_gmu_hfi_gen7_bw_table *table)
{
    gpu_gmu_hfi_gen7_bw_message message = {0};

    if (!table || !table->level_count || !table->cnoc_command_count ||
        !table->ddr_command_count ||
        table->level_count > GPU_GMU_HFI_GEN7_MAX_BW_LEVELS ||
        table->cnoc_command_count > GPU_GMU_HFI_GEN7_MAX_CNOC_COMMANDS ||
        table->ddr_command_count > GPU_GMU_HFI_GEN7_MAX_DDR_COMMANDS)
        return -1;
    message.level_count = table->level_count;
    message.cnoc_command_count = table->cnoc_command_count;
    message.ddr_command_count = table->ddr_command_count;
    message.cnoc_wait_mask = table->cnoc_wait_mask;
    message.ddr_wait_mask = table->ddr_wait_mask;
    for (uint32_t i = 0u; i < table->cnoc_command_count; ++i)
    {
        message.cnoc_addresses[i] = table->cnoc_addresses[i];
        message.cnoc_data[0u][i] = table->cnoc_data[0u][i];
        message.cnoc_data[1u][i] = table->cnoc_data[1u][i];
    }
    for (uint32_t level = 0u; level < table->level_count; ++level)
        for (uint32_t command = 0u; command < table->ddr_command_count;
             ++command)
            message.ddr_data[level][command] = table->ddr_data[level][command];
    for (uint32_t i = 0u; i < table->ddr_command_count; ++i)
        message.ddr_addresses[i] = table->ddr_addresses[i];
    return gen7_request(gmu, memory, GPU_GMU_HFI_H2F_BW_TABLE,
                        (uint32_t *)(void *)&message,
                        (uint32_t)(sizeof(message) / sizeof(uint32_t)));
}

int gpu_gmu_hfi_gen7_send_core_fw_start(const gpu_mmio_window *gmu,
                                        gpu_gmu_memory *memory)
{
    uint32_t message[2u] = {0u, 0u};

    return gen7_request(gmu, memory, GPU_GMU_HFI_H2F_CORE_FW_START,
                        message, 2u);
}

int gpu_gmu_hfi_gen7_send_start(const gpu_mmio_window *gmu,
                                gpu_gmu_memory *memory)
{
    uint32_t message = 0u;

    return gen7_request(gmu, memory, GPU_GMU_HFI_H2F_START, &message, 1u);
}

int gpu_gmu_hfi_gen7_set_gx_bw(const gpu_mmio_window *gmu,
                               gpu_gmu_memory *memory,
                               uint32_t frequency_index,
                               uint32_t bandwidth_index)
{
    /* Header, ack_type (blocking), performance-table index, BW-table index. */
    uint32_t message[4u] = {0u, 1u, frequency_index, bandwidth_index};

    return gen7_request(gmu, memory, GPU_GMU_HFI_H2F_GX_BW_PERF_VOTE,
                        message, 4u);
}
