#pragma once
#include <stdint.h>
typedef int (*a64_attr_read)(uint64_t address, uint64_t *value);
/* The initial table can be shorter than 512 entries. Child tables are full.
 * Reader must reject inaccessible addresses; no unchecked dereferences here. */
int a64_attr_scan(uint64_t table, unsigned level, unsigned entries,
                  uint32_t *used, uint32_t *budget, a64_attr_read read);
int a64_attr_root_shape(unsigned tsz, unsigned *level, unsigned *entries);
