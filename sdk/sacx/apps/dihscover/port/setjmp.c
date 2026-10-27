#include "setjmp.h"
#if defined(__aarch64__)
__attribute__((naked,returns_twice)) int setjmp(jmp_buf state) {
 __asm__ volatile("stp x19,x20,[x0,#0]\n stp x21,x22,[x0,#16]\n stp x23,x24,[x0,#32]\n"
 "stp x25,x26,[x0,#48]\n stp x27,x28,[x0,#64]\n stp x29,x30,[x0,#80]\n"
 "mov x9,sp\n str x9,[x0,#96]\n stp d8,d9,[x0,#104]\n stp d10,d11,[x0,#120]\n"
 "stp d12,d13,[x0,#136]\n stp d14,d15,[x0,#152]\n mrs x9,fpcr\n str x9,[x0,#168]\n mrs x9,fpsr\n str x9,[x0,#176]\n mov w0,#0\n ret\n");
}
__attribute__((naked,noreturn)) void longjmp(jmp_buf state,int result) {
 __asm__ volatile("ldp x19,x20,[x0,#0]\n ldp x21,x22,[x0,#16]\n ldp x23,x24,[x0,#32]\n"
 "ldp x25,x26,[x0,#48]\n ldp x27,x28,[x0,#64]\n ldp x29,x30,[x0,#80]\n"
 "ldr x9,[x0,#96]\n mov sp,x9\n ldp d8,d9,[x0,#104]\n ldp d10,d11,[x0,#120]\n"
 "ldp d12,d13,[x0,#136]\n ldp d14,d15,[x0,#152]\n ldr x9,[x0,#168]\n msr fpcr,x9\n ldr x9,[x0,#176]\n msr fpsr,x9\n cmp w1,#0\n csinc w0,w1,wzr,ne\n ret\n");
}
#elif defined(__x86_64__) && defined(DIHSCOVER_FREESTANDING)
/* Bare-metal x64 uses the SysV ABI, unlike the Windows host font tests. */
__attribute__((naked,returns_twice)) int setjmp(jmp_buf state) {
 __asm__ volatile("mov %rbx,0(%rdi)\n mov %rbp,8(%rdi)\n mov %r12,16(%rdi)\n mov %r13,24(%rdi)\n mov %r14,32(%rdi)\n mov %r15,40(%rdi)\n"
 "lea 8(%rsp),%rax\n mov %rax,48(%rdi)\n mov (%rsp),%rax\n mov %rax,56(%rdi)\n stmxcsr 64(%rdi)\n fnstcw 68(%rdi)\n xor %eax,%eax\n ret\n");
}
__attribute__((naked,noreturn)) void longjmp(jmp_buf state,int result) {
 __asm__ volatile("mov %esi,%eax\n test %eax,%eax\n jne 1f\n inc %eax\n 1: mov 0(%rdi),%rbx\n mov 8(%rdi),%rbp\n mov 16(%rdi),%r12\n mov 24(%rdi),%r13\n mov 32(%rdi),%r14\n mov 40(%rdi),%r15\n ldmxcsr 64(%rdi)\n fldcw 68(%rdi)\n mov 48(%rdi),%rsp\n jmp *56(%rdi)\n");
}
#endif
