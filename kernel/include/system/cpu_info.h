#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define CPU_INFO_MAX_CORES 32u

typedef struct cpu_core_info
{
    uint32_t acpi_uid;
    uint32_t gicc_id;
    uint32_t flags;
    uint32_t enabled;
    uint64_t mpidr;
} cpu_core_info;

typedef struct cpu_info_snapshot
{
    uint32_t initialized;
    uint32_t acpi_madt_found;
    uint32_t core_count;
    uint32_t enabled_count;
    uint64_t boot_mpidr;
    uint64_t midr;
    uint64_t revidr;
    uint64_t ctr;
    uint64_t id_aa64pfr0;
    uint64_t id_aa64isar0;
    uint64_t id_aa64mmfr0;
    cpu_core_info cores[CPU_INFO_MAX_CORES];
} cpu_info_snapshot;

void cpu_info_init(uint64_t acpi_rsdp_phys);
void cpu_info_get(cpu_info_snapshot *out_snapshot);

#ifdef __cplusplus
}
#endif
