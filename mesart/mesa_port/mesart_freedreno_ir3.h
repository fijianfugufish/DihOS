#pragma once

/* First freestanding slice of Mesa's IR3 shader backend.  It deliberately
 * exposes a tiny stable bridge while the larger NIR/compiler dependency graph
 * is ported behind it. */
int mesart_fd_ir3_vocabulary_selftest(void);
