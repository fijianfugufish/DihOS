#include "asm/aa64_user_slice.h"
#include <stddef.h>
#include "process/dihos_user_sacx.h"

#if defined(__aarch64__)
_Static_assert(sizeof(aa64_user_frame)==832,"EL0 assembly frame size");
_Static_assert(offsetof(aa64_user_frame,q)==288,"EL0 vector register offset");
_Static_assert(offsetof(aa64_user_frame,tpidr)==816,"EL0 TLS offset");
_Static_assert(offsetof(aa64_user_cpu,root_phys)==832,"EL0 translation root offset");
typedef struct user_return_context {
    uint64_t root,daif,x[13],d[8],fpcr,fpsr,tpidr,cpacr;
    aa64_user_cpu *active;
    uint64_t result;
} user_return_context;
_Static_assert(offsetof(user_return_context,active)==216,"EL1 context layout");
/* Entry/return assembly also writes these fields outside the C data-flow. */
static volatile user_return_context user_return __attribute__((used));
static void user_return_trampoline(void) __attribute__((naked));
static int user_enter_slice(aa64_user_cpu *task) __attribute__((naked));

int aa64_user_slice_active(void){return user_return.active!=0;}

/* Always return to the EL1 scheduler after a syscall. No file, network, image
 * decoder or application callback is executed on this exception boundary. */
static int user_slice_dispatch(aa64_user_frame *frame,uint64_t irq) __attribute__((used));
static int user_slice_dispatch(aa64_user_frame *frame,uint64_t irq)
{
    aa64_user_cpu *task=user_return.active;
    if(!task)return irq?0:aa64_el0_sync_dispatch(&frame->fault);
    int event=AA64_USER_FAULTED;
    if(irq) {
        int ack=aa64_user_timer_ack();
        if(!ack)return 1;
        event=ack>0?AA64_USER_PREEMPTED:AA64_USER_IRQ_ERROR;
    }else if(((frame->fault.esr>>26)&63)==0x15) {
        uint64_t syscall=frame->fault.x[8];
        if(syscall==SACX_USER_SYSCALL)event=AA64_USER_SACX_REQUEST;
        else if(syscall==DIHOS_EL0_SYSCALL_EXIT){task->exit_status=frame->fault.x[0];event=AA64_USER_EXITED;}
        else if(syscall==DIHOS_EL0_SYSCALL_SLEEP_MS && frame->fault.x[0]<=60000){task->sleep_ms=frame->fault.x[0];frame->fault.x[0]=0;event=AA64_USER_SLEEPING;}
        else {frame->fault.x[0]=syscall==DIHOS_EL0_SYSCALL_YIELD?0:(uint64_t)-38;event=AA64_USER_YIELDED;}
    }
    aa64_user_timer_disarm();
    if(event==AA64_USER_FAULTED){
        uint64_t saved_par;
        __asm__ volatile("mrs %0,ttbr0_el1":"=r"(task->fault_root));
        __asm__ volatile("mrs %0,sctlr_el1":"=r"(task->fault_sctlr));
        __asm__ volatile("mrs %0,par_el1":"=r"(saved_par));
        __asm__ volatile("at s1e0r,%0; isb"::"r"(frame->fault.far):"memory");
        __asm__ volatile("mrs %0,par_el1":"=r"(task->fault_par));
        __asm__ volatile("msr par_el1,%0"::"r"(saved_par):"memory");
    }
    task->frame=*frame; ++task->slices;
    user_return.result=(uint64_t)event;
    __asm__ volatile("dsb ishst; msr ttbr0_el1,%0; isb; tlbi vmalle1is; dsb ish; isb"::"r"(user_return.root):"memory");
    frame->fault.elr=(uintptr_t)user_return_trampoline;
    frame->fault.spsr=user_return.daif|5u;
    return 1;
}

int aa64_user_slice_run(aa64_user_cpu *task,uint32_t microseconds)
{
    if(!task || !task->root_phys || user_return.active)return -1;
    if((task->frame.sp&15) || (task->frame.fault.elr&3))return -1;
    if(aa64_user_timer_arm(microseconds))return -2;
    task->quantum_ticks=aa64_user_timer_ticks();
    /* Only NZCV is carried over; users cannot change exception masks or EL. */
    task->frame.fault.spsr=(task->frame.fault.spsr&0xf0000000u)|0x340u;
    int result=user_enter_slice(task);
    aa64_user_timer_disarm();
    return result;
}

#include "aa64_user_vectors.inc"
#else
int aa64_user_slice_active(void){return 0;}
int aa64_user_slice_run(aa64_user_cpu *task,uint32_t us){(void)task;(void)us;return -1;}
void aa64_user_slice_sync_vector(void){}
void aa64_user_slice_irq_vector(void){}
#endif
