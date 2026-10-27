#include "system/kfiber.h"
static kfiber *running;
#if defined(__aarch64__)
__attribute__((naked,noinline)) static void swap_context(uint64_t *from,uint64_t *to)
{
    __asm__ volatile(
        "stp x19,x20,[x0,#0]\n stp x21,x22,[x0,#16]\n stp x23,x24,[x0,#32]\n"
        "stp x25,x26,[x0,#48]\n stp x27,x28,[x0,#64]\n stp x29,x30,[x0,#80]\n"
        "mov x9,sp\n str x9,[x0,#96]\n"
        "stp d8,d9,[x0,#104]\n stp d10,d11,[x0,#120]\n"
        "stp d12,d13,[x0,#136]\n stp d14,d15,[x0,#152]\n"
        "mrs x9,fpcr\n str x9,[x0,#168]\n mrs x9,fpsr\n str x9,[x0,#176]\n"
        "ldp x19,x20,[x1,#0]\n ldp x21,x22,[x1,#16]\n ldp x23,x24,[x1,#32]\n"
        "ldp x25,x26,[x1,#48]\n ldp x27,x28,[x1,#64]\n ldp x29,x30,[x1,#80]\n"
        "ldr x9,[x1,#96]\n mov sp,x9\n"
        "ldp d8,d9,[x1,#104]\n ldp d10,d11,[x1,#120]\n"
        "ldp d12,d13,[x1,#136]\n ldp d14,d15,[x1,#152]\n"
        "ldr x9,[x1,#168]\n msr fpcr,x9\n ldr x9,[x1,#176]\n msr fpsr,x9\n ret\n");
}
#elif defined(__x86_64__) || defined(_M_X64)
__attribute__((naked,noinline)) static void swap_context(uint64_t *from,uint64_t *to)
{
    __asm__ volatile(
#if defined(_WIN32)
        "mov %rcx,%r10\n mov %rdx,%r11\n"
#else
        "mov %rdi,%r10\n mov %rsi,%r11\n"
#endif
        "mov %rbx,0(%r10)\n mov %rbp,8(%r10)\n mov %r12,16(%r10)\n"
        "mov %r13,24(%r10)\n mov %r14,32(%r10)\n mov %r15,40(%r10)\n"
        "mov %rsp,48(%r10)\n lea 1f(%rip),%rax\n mov %rax,56(%r10)\n"
        "movdqu %xmm6,64(%r10)\n movdqu %xmm7,80(%r10)\n movdqu %xmm8,96(%r10)\n"
        "movdqu %xmm9,112(%r10)\n movdqu %xmm10,128(%r10)\n movdqu %xmm11,144(%r10)\n"
        "movdqu %xmm12,160(%r10)\n movdqu %xmm13,176(%r10)\n"
        "movdqu %xmm14,192(%r10)\n movdqu %xmm15,208(%r10)\n"
        "stmxcsr 224(%r10)\n fnstcw 228(%r10)\n"
        "mov %rdi,232(%r10)\n mov %rsi,240(%r10)\n"
        "mov 0(%r11),%rbx\n mov 8(%r11),%rbp\n mov 16(%r11),%r12\n"
        "mov 24(%r11),%r13\n mov 32(%r11),%r14\n mov 40(%r11),%r15\n"
        "movdqu 64(%r11),%xmm6\n movdqu 80(%r11),%xmm7\n movdqu 96(%r11),%xmm8\n"
        "movdqu 112(%r11),%xmm9\n movdqu 128(%r11),%xmm10\n movdqu 144(%r11),%xmm11\n"
        "movdqu 160(%r11),%xmm12\n movdqu 176(%r11),%xmm13\n"
        "movdqu 192(%r11),%xmm14\n movdqu 208(%r11),%xmm15\n"
        "ldmxcsr 224(%r11)\n fldcw 228(%r11)\n"
        "mov 232(%r11),%rdi\n mov 240(%r11),%rsi\n"
        "mov 48(%r11),%rsp\n jmp *56(%r11)\n 1: ret\n");
}
#else
#error Unsupported fiber architecture
#endif
static void enter_fiber(void)
{
    kfiber *fiber=running;
    fiber->entry(fiber->argument);
    fiber->state=KFIBER_DONE;
    swap_context(fiber->context,fiber->caller);
    for (;;) {}
}
int kfiber_init(kfiber *fiber,void *stack,uint64_t bytes,void (*entry)(void *),void *argument)
{
    if (!fiber || !stack || bytes<4096 || !entry) return -1;
    *fiber=(kfiber){0};fiber->entry=entry;fiber->argument=argument;
    uintptr_t top=((uintptr_t)stack+bytes)&~(uintptr_t)15;
#if defined(__aarch64__)
    fiber->context[12]=top;fiber->context[11]=(uintptr_t)enter_fiber;
#else
    top-=40;*(uint64_t *)top=0;
    fiber->context[6]=top;fiber->context[7]=(uintptr_t)enter_fiber;
    fiber->context[28]=0x037f00001f80ull; /* x87 control + MXCSR */
#endif
    fiber->state=KFIBER_READY;return 0;
}
int kfiber_resume(kfiber *fiber)
{
    if (!fiber || running || (fiber->state!=KFIBER_READY && fiber->state!=KFIBER_SUSPENDED)) return -1;
    running=fiber;fiber->state=KFIBER_RUNNING;
    swap_context(fiber->caller,fiber->context);
    running=0;return fiber->state==KFIBER_DONE ? 1:0;
}
void kfiber_yield(void)
{
    kfiber *fiber=running;if (!fiber) return;
    fiber->state=KFIBER_SUSPENDED;
    swap_context(fiber->context,fiber->caller);
    fiber->state=KFIBER_RUNNING;
}

int kfiber_is_current(const kfiber *fiber) {return running==fiber;}
