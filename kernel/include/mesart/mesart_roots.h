#pragma once

#include "mesart/mesart_trust.h"

/* Returns null until the key appropriate for this kernel configuration has
 * been explicitly provisioned. */
const mesart_trust_root *mesart_kernel_trust_root(void);

