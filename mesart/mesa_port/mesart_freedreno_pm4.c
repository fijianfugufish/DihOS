#include "freedreno_pm4.h"

#include "mesart_freedreno_pm4.h"

uint32_t mesart_fd_a7xx_pkt7(uint32_t opcode, uint32_t payload_dwords)
{
    if (opcode > 0x7fu || payload_dwords > 0x3fffu)
        return 0u;
    return pm4_pkt7_hdr((uint8_t)opcode, (uint16_t)payload_dwords);
}

int mesart_fd_mesa_pm4_selftest(void)
{
    /* CP opcode values and the odd-parity Type-7 framing come from Mesa's
     * generated adreno_pm4.xml.h plus freedreno_pm4.h, not a copied local
     * table.  These exact words are the only non-NOP forms Mesart admits. */
    if (mesart_fd_a7xx_pkt7(CP_MEM_WRITE, 3u) != 0x703d8003u ||
        mesart_fd_a7xx_pkt7(CP_WAIT_MEM_WRITES, 0u) != 0x70928000u)
        return -1;
    return 0;
}
