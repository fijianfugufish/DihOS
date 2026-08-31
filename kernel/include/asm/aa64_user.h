#pragma once

#include <stdint.h>
#include "memory/aarch64_user_vm.h"

/* Register image saved for a synchronous exception from EL0 AArch64. */
typedef struct aa64_el0_frame
{
    uint64_t x[31];
    uint64_t esr;
    uint64_t far;
    uint64_t elr;
    uint64_t spsr;
} aa64_el0_frame;

/* A service-specific EL0 syscall broker.  It receives only the register frame
 * and its kernel-owned context; it must return nonzero only when it handled
 * the request. */
typedef int (*aa64_user_syscall_handler)(aa64_el0_frame *frame,
                                         void *context);

/* Deliberately tiny initial syscall surface.  More operations are admitted
 * only through process capabilities, never by exposing a Linux ABI. */
#define DIHOS_EL0_SYSCALL_EXIT 0u
/* An EL0 exception that is not an admitted syscall is reported to the
 * supervising kernel as an exit status with this high-bit marker. */
#define DIHOS_EL0_EXIT_FAULT (1ull << 63)

/* Enters an already-built user VM at entry_va with an EL0 stack.  The call
 * returns only after DIHOS_EL0_SYSCALL_EXIT has restored the caller's EL1
 * translation root; no process is started automatically. */
int aa64_user_enter(const aarch64_user_vm *vm, uint64_t entry_va,
                    uint64_t user_stack_top, uint64_t *out_exit_status);
/* Same isolated entry path, with a narrowly scoped kernel broker for one
 * verified service.  Passing a null handler is equivalent to aa64_user_enter.
 */
int aa64_user_enter_with_handler(const aarch64_user_vm *vm, uint64_t entry_va,
                                 uint64_t user_stack_top,
                                 uint64_t *out_exit_status,
                                 aa64_user_syscall_handler handler,
                                 void *handler_context);
/* Explicit smoke test for the EL0 entry/exit path; never invoked at boot. */
int aa64_user_selftest(uint64_t *out_exit_status);

/* Called by the lower-EL synchronous vector.  A zero result means the fault
 * is not an admitted EL0 syscall and must receive the normal panic report. */
int aa64_el0_sync_dispatch(aa64_el0_frame *frame);
