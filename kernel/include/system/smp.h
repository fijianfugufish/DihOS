#pragma once

#include <stdint.h>
#include "system/cpu_info.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define SMP_MAX_CORES CPU_INFO_MAX_CORES

enum
{
    SMP_CORE_ABSENT = 0u,
    SMP_CORE_BOOT = 1u,
    SMP_CORE_DISCOVERED = 2u,
    SMP_CORE_STARTING = 3u,
    SMP_CORE_ONLINE = 4u,
    SMP_CORE_FAILED = 5u,
};

typedef struct smp_core_status
{
    uint32_t logical_id;
    uint32_t state;
    uint32_t acpi_uid;
    uint32_t enabled;
    uint32_t busy;
    uint32_t jobs_completed;
    uint32_t poll_ticks;
    uint32_t worker_stage;
    uint64_t worker_poll_entry;
    int64_t last_psci_status;
    uint64_t mpidr;
} smp_core_status;

typedef struct smp_snapshot
{
    uint32_t initialized;
    uint32_t psci_available;
    uint32_t psci_conduit;
    uint32_t core_count;
    uint32_t online_count;
    uint32_t worker_count;
    smp_core_status cores[SMP_MAX_CORES];
} smp_snapshot;

typedef void (*smp_worker_poll_fn)(uint32_t logical_id);

void smp_init(uint64_t acpi_rsdp_phys);
void smp_set_worker_poll(smp_worker_poll_fn fn);
void smp_signal_workers(void);
uint32_t smp_current_logical_id(void);
void smp_mark_busy(uint32_t logical_id, uint32_t busy);
void smp_mark_job_done(uint32_t logical_id);
void smp_get_snapshot(smp_snapshot *out_snapshot);

#ifdef __cplusplus
}
#endif
