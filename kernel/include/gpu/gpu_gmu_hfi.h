#pragma once

#include <stdint.h>
#include "gpu/gpu_gmu_memory.h"
#include "gpu/gpu_mmio.h"

#define GPU_GMU_HFI_COMMAND_QUEUE  0u
#define GPU_GMU_HFI_RESPONSE_QUEUE 1u

/* Gen7 GMU startup uses firmware-owned HFI tables.  The common types below
 * deliberately describe data, rather than Qualcomm registers or an RPMh
 * transport, so another GPU family can supply its own power service and
 * reuse the queue protocol. */
#define GPU_GMU_HFI_GEN7_MAX_GX_LEVELS    16u
#define GPU_GMU_HFI_GEN7_MAX_CX_LEVELS     4u
#define GPU_GMU_HFI_GEN7_MAX_CNOC_COMMANDS 6u
#define GPU_GMU_HFI_GEN7_MAX_DDR_COMMANDS  8u
#define GPU_GMU_HFI_GEN7_MAX_BW_LEVELS    16u

typedef struct gpu_gmu_hfi_gen7_gx_level
{
    uint32_t power_vote;
    uint32_t acd;
    uint32_t frequency_khz;
} gpu_gmu_hfi_gen7_gx_level;

typedef struct gpu_gmu_hfi_gen7_cx_level
{
    uint32_t power_vote;
    uint32_t frequency_khz;
} gpu_gmu_hfi_gen7_cx_level;

typedef struct gpu_gmu_hfi_gen7_perf_table
{
    uint32_t gx_level_count;
    uint32_t cx_level_count;
    gpu_gmu_hfi_gen7_gx_level gx[GPU_GMU_HFI_GEN7_MAX_GX_LEVELS];
    gpu_gmu_hfi_gen7_cx_level cx[GPU_GMU_HFI_GEN7_MAX_CX_LEVELS];
} gpu_gmu_hfi_gen7_perf_table;

typedef struct gpu_gmu_hfi_gen7_bw_table
{
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
} gpu_gmu_hfi_gen7_bw_table;

/* Sends one fully-formed HFI request through queue 0.  The routine owns the
 * HFI packet header and returns its sequence number; it never enables or
 * starts the GMU. */
int gpu_gmu_hfi_send(const gpu_mmio_window *gmu, gpu_gmu_memory *memory,
                     uint8_t message_id, const uint32_t *payload,
                     uint32_t payload_dwords, uint32_t *out_sequence);

/* Waits for a response interrupt and copies one response queue packet.  This
 * is polling-only until DihOS has installed the GMU Host SPI handler. */
int gpu_gmu_hfi_read_response(const gpu_mmio_window *gmu,
                              gpu_gmu_memory *memory,
                              uint32_t *out_words, uint32_t capacity_dwords,
                              uint32_t *out_dwords);

/* Synchronous command helper for early bring-up.  It sends one request and
 * verifies that the response acknowledges its exact sequence number before
 * exposing any response payload to the caller. */
int gpu_gmu_hfi_request(const gpu_mmio_window *gmu, gpu_gmu_memory *memory,
                        uint8_t message_id, const uint32_t *payload,
                        uint32_t payload_dwords, uint32_t *out_payload,
                        uint32_t payload_capacity_dwords,
                        uint32_t *out_payload_dwords);

/* Stage-specific Gen7 HFI messages.  These functions only configure the GMU
 * already running on HFI; they do not make a GX power request or write GPU
 * command-processor registers. */
int gpu_gmu_hfi_gen7_send_perf_table(const gpu_mmio_window *gmu,
                                     gpu_gmu_memory *memory,
                                     const gpu_gmu_hfi_gen7_perf_table *table);
int gpu_gmu_hfi_gen7_send_bw_table(const gpu_mmio_window *gmu,
                                   gpu_gmu_memory *memory,
                                   const gpu_gmu_hfi_gen7_bw_table *table);
int gpu_gmu_hfi_gen7_send_core_fw_start(const gpu_mmio_window *gmu,
                                        gpu_gmu_memory *memory);
int gpu_gmu_hfi_gen7_send_start(const gpu_mmio_window *gmu,
                                gpu_gmu_memory *memory);

/* Requests one blocking GX frequency/bandwidth vote after Gen7 HFI START.
 * The indices are entries in the already accepted performance and bandwidth
 * tables; this does not access GX registers directly. */
int gpu_gmu_hfi_gen7_set_gx_bw(const gpu_mmio_window *gmu,
                               gpu_gmu_memory *memory,
                               uint32_t frequency_index,
                               uint32_t bandwidth_index);
