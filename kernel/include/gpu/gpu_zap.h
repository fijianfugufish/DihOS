#pragma once
#include <stdint.h>
#include "gpu/gpu_firmware.h"

/* Deliberately limited to the monolithic, one-load-segment X1-85 ELF32 MBN. */
typedef struct gpu_zap_layout {
    uint32_t header_bytes, hash_offset, hash_bytes;
    uint32_t code_offset, code_bytes, memory_bytes;
} gpu_zap_layout;
int gpu_zap_parse(const void *data, uint64_t bytes, gpu_zap_layout *out);
/* One attempt per boot, retained private allocations, no signature bypass. */
int gpu_zap_start(const gpu_firmware_set *firmware);
