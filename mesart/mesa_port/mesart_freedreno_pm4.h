#pragma once

#include <stdint.h>

/* Narrow wrapper around Mesa's generated PM4 helpers.  The rest of Mesart
 * depends on this stable bridge instead of reaching into Mesa headers. */
uint32_t mesart_fd_a7xx_pkt7(uint32_t opcode, uint32_t payload_dwords);

/* Confirms the upstream generated packet vocabulary agrees with the narrow
 * broker allow-list before EL0 emits a live command batch. */
int mesart_fd_mesa_pm4_selftest(void);
