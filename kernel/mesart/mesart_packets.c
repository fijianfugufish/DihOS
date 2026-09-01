#include "mesart/mesart_packets.h"

#define MESART_A7XX_TYPE7_HEADER_MASK  0xf0000000u
#define MESART_A7XX_TYPE7_HEADER_VALUE 0x70000000u
#define MESART_A7XX_TYPE7_RESERVED     0x0f000000u
#define MESART_A7XX_TYPE7_OPCODE_MASK  0x007f0000u
#define MESART_A7XX_TYPE7_COUNT_MASK   0x00003fffu
#define MESART_A7XX_CP_NOP             0x10u

static uint32_t mesart_odd_parity(uint32_t value)
{
    value ^= value >> 4;
    value ^= value >> 8;
    value ^= value >> 16;
    return (0x9669u >> (value & 0x0fu)) & 1u;
}

static int mesart_is_a7xx_type7(uint32_t word)
{
    uint32_t opcode;
    uint32_t payload_dwords;

    if ((word & MESART_A7XX_TYPE7_HEADER_MASK) !=
            MESART_A7XX_TYPE7_HEADER_VALUE ||
        (word & MESART_A7XX_TYPE7_RESERVED) != 0u)
        return 0;
    opcode = (word & MESART_A7XX_TYPE7_OPCODE_MASK) >> 16;
    payload_dwords = word & MESART_A7XX_TYPE7_COUNT_MASK;
    return ((word >> 23) & 1u) == mesart_odd_parity(opcode) &&
           ((word >> 15) & 1u) == mesart_odd_parity(payload_dwords);
}

int mesart_validate_a7xx_cp_stream(const uint32_t *dwords, uint32_t count,
                                   mesart_packet_validation *out_validation)
{
    uint32_t at = 0u;
    uint32_t packets = 0u;

    if (out_validation)
        *out_validation = (mesart_packet_validation){0};
    if (!dwords || !count)
        return -1;
    while (at < count)
    {
        uint32_t header = dwords[at];
        uint32_t opcode;
        uint32_t payload_dwords;

        if (!mesart_is_a7xx_type7(header))
            return -2;
        opcode = (header & MESART_A7XX_TYPE7_OPCODE_MASK) >> 16;
        payload_dwords = header & MESART_A7XX_TYPE7_COUNT_MASK;
        if (payload_dwords > count - at - 1u)
            return -3;
        if (opcode != MESART_A7XX_CP_NOP)
            return -4;
        at += payload_dwords + 1u;
        ++packets;
    }
    if (out_validation)
        *out_validation = (mesart_packet_validation){packets, count};
    return 0;
}
