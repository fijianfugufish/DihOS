#pragma once

/* Executes upstream Mesa's generated Freedreno device lookup against the
 * brokered X1-85 chip ID.  This is the first Mesa source linked into Mesart. */
int mesart_fd_mesa_device_selftest(void);
