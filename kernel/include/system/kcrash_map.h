#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kcrash_location
{
    char function[96];
    char file[160];
    uint32_t line;
} kcrash_location;

/* Load the build-generated DWARF address map while the filesystem is healthy. */
int kcrash_map_load(const char *path);

/* Resolve a runtime program counter using the physical load base from bootinfo. */
int kcrash_map_lookup(uint64_t runtime_pc, uint64_t load_base, kcrash_location *out);

#ifdef __cplusplus
}
#endif
