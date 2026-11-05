#pragma once
#include "asm/aa64_user.h"

/* Private kernel state, never a user-writable syscall structure. FPSIMD is
 * saved in full; SVE/SME are disabled while this initial EL0 path executes. */
typedef struct aa64_user_frame {
    aa64_el0_frame fault;
    uint64_t sp;
    uint64_t q[64];
    uint64_t fpcr, fpsr, tpidr, reserved;
} aa64_user_frame;

enum {
    AA64_USER_PREEMPTED=1, AA64_USER_YIELDED, AA64_USER_SLEEPING,
    AA64_USER_EXITED, AA64_USER_FAULTED, AA64_USER_IRQ_ERROR, AA64_USER_SACX_REQUEST
};
#define DIHOS_EL0_SYSCALL_YIELD 1u
#define DIHOS_EL0_SYSCALL_SLEEP_MS 2u

typedef struct aa64_user_cpu {
    aa64_user_frame frame;
    uint64_t root_phys;
    uint64_t exit_status, sleep_ms, slices, quantum_ticks;
    uint64_t fault_root, fault_par, fault_sctlr;
} aa64_user_cpu;

typedef struct aa64_user_timer_config {
    uint64_t distributor, redistributor, redistributor_bytes;
    uint32_t irq, flags; /* GTDT: bit 0 edge trigger, otherwise level. */
} aa64_user_timer_config;

typedef struct aa64_user_timer_diagnostic {
    aa64_user_timer_config config;
    uint64_t current_core, initialized_core;
    uint32_t was_ready;
    int32_t result;
} aa64_user_timer_diagnostic;
int aa64_user_timer_validate(const aa64_user_timer_config *config);
void aa64_user_timer_get_diagnostic(aa64_user_timer_diagnostic *out);

/* Explicit boot-core opt-in only. Does not unmask EL1 interrupts. Unsupported
 * platforms return an error; they must never fall back to unbounded EL0 entry. */
int aa64_user_timer_init(const aa64_user_timer_config *config);
#define AA64_USER_TIMER_MIN_US 100u
#define AA64_USER_TIMER_MAX_US 5000u
int aa64_user_timer_arm(uint32_t microseconds);
uint64_t aa64_user_timer_ticks(void);
void aa64_user_timer_disarm(void);
int aa64_user_timer_ack(void); /* 1 timer, 0 spurious, -1 other interrupt */
int aa64_user_slice_run(aa64_user_cpu *task, uint32_t microseconds);
int aa64_user_slice_active(void);
void aa64_user_slice_sync_vector(void);
void aa64_user_slice_irq_vector(void);
