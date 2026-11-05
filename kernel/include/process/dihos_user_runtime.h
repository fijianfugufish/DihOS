#pragma once
#include "process/dihos_process.h"

#define DIHOS_USER_TASKS 4u
enum { DIHOS_USER_LOOP=1, DIHOS_USER_EXIT, DIHOS_USER_FAULT, DIHOS_USER_SLEEP };
enum { DIHOS_USER_READY=1, DIHOS_USER_WAITING, DIHOS_USER_DONE, DIHOS_USER_FAILED, DIHOS_USER_STOPPED, DIHOS_USER_LOADING };
typedef struct dihos_user_task_info {
    dihos_process_handle handle;
    uint32_t kind,state;
    uint64_t slices,preemptions,counter,pc,esr,far,exit_status;
    uint32_t admission_step;
    uint64_t load_done,load_total;
    int32_t admission_result;
    uint64_t code_va, stack_va;
    uint32_t backing_alias;
    uint64_t fault_walk[4], tcr;
    int32_t fault_level;
    uint64_t hardware_root, expected_root, hardware_par, sctlr;
    uint64_t sacx_calls,granted_capabilities;
    int64_t sacx_last_result;
    uint64_t memory_reserved,memory_committed,memory_peak;
    uint32_t relocations;
    uint32_t threads_live,tls_bytes;
    uint64_t threads_created,threads_exited,thread_joins,thread_waits,thread_wakes,thread_timeouts,fault_thread;
    uint32_t threads_held;
    uint64_t inspection_owner,thread_detaches;
    char last_log[257];
} dihos_user_task_info;

/* Initial native-task exercise, not an engine loader or a new SACX ABI.
 * Enabling is explicit; no GIC/timer configuration occurs during normal boot. */
int dihos_user_runtime_enable(void);
int dihos_user_spawn_fixture(uint32_t kind,dihos_process_handle *out);
void dihos_user_runtime_poll(void);
/* Desktop pump: <=64 steps; 1 ms normally, <=4 ms for a kernel-selected
 * interactive process. Stops on preemption,
 * explicit yield/log, admission or exit. Kernel service cost may overshoot. */
uint32_t dihos_user_runtime_service(void);
/* Kernel-owned interactive process only. No app can change its own quantum.
 * Keeps timer preemption, round-robin selection and the 64-step bound. */
int dihos_user_runtime_set_interactive(dihos_process_handle,uint32_t quantum_us);
int dihos_user_runtime_stop(dihos_process_handle handle);
void dihos_user_runtime_stop_all(void);
uint32_t dihos_user_runtime_snapshot(dihos_user_task_info *out,uint32_t capacity);
int dihos_user_spawn_elf(const void *data,uint64_t bytes,dihos_process_handle *out);
/* On success, consumes pmem-owned page-aligned source. Caller retains it on error. */
int dihos_user_spawn_owned_elf(void *data,uint64_t bytes,uint64_t pages,dihos_process_handle *out);
/* Explicit kernel launch grant; ordinary launches have no network capability. */
int dihos_user_spawn_owned_elf_with_network(void *data,uint64_t bytes,uint64_t pages,uint32_t network,dihos_process_handle *out);
/* Kernel-selected extra capabilities, limited to network and owned surfaces. */
int dihos_user_spawn_owned_elf_with_grants(void *data,uint64_t bytes,uint64_t pages,uint64_t grants,dihos_process_handle *out);
/* Trusted browser-loader entry. Not exposed as an application syscall. */
int dihos_user_spawn_owned_browser_elf(void *data,uint64_t bytes,uint64_t pages,dihos_process_handle *out);
