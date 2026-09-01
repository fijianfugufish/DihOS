#pragma once

#include <stdint.h>

/*
 * First-stage A7xx command validation for Mesart.  This is deliberately a
 * narrow allow-list, not a claim that arbitrary Freedreno streams are safe.
 * Inert NOPs, a bounded write to the renderer-owned resource arena, and the
 * zero-payload CP_WAIT_MEM_WRITES ordering primitive are accepted.  Register
 * writes, indirect buffers, and all unknown packet types fail closed until
 * their buffer references and state effects have a broker.
 */
typedef struct mesart_packet_validation
{
    uint32_t packet_count;
    uint32_t dword_count;
} mesart_packet_validation;

typedef struct mesart_packet_policy
{
    uint64_t writable_gpu_va;
    uint64_t writable_gpu_bytes;
    uint32_t allow_resource_write;
    uint32_t allow_wait_mem_writes;
} mesart_packet_policy;

int mesart_validate_a7xx_cp_stream(const uint32_t *dwords, uint32_t count,
                                   const mesart_packet_policy *policy,
                                   mesart_packet_validation *out_validation);
