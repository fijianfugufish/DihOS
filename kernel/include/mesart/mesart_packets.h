#pragma once

#include <stdint.h>

/*
 * First-stage A7xx command validation for Mesart.  This is deliberately a
 * narrow allow-list, not a claim that arbitrary Freedreno streams are safe:
 * only correctly framed CP_NOP Type-7 packets are accepted.  Register writes,
 * indirect buffers, memory operations, and all unknown packet types fail
 * closed until their buffer references and state effects have a broker.
 */
typedef struct mesart_packet_validation
{
    uint32_t packet_count;
    uint32_t dword_count;
} mesart_packet_validation;

int mesart_validate_a7xx_cp_stream(const uint32_t *dwords, uint32_t count,
                                   mesart_packet_validation *out_validation);
