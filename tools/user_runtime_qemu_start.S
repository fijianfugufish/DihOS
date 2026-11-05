.section .text.boot,"ax"
.global _start
_start:
 msr daifset,#15
 mrs x0,CurrentEL
 cmp x0,#8
 b.ne 1f
 mov x0,#0x80000000
 msr hcr_el2,x0
 mov x0,#3
 msr cnthctl_el2,x0
 msr cntvoff_el2,xzr
 msr cptr_el2,xzr
 mov x0,#9
 msr icc_sre_el2,x0
 adr x0,1f
 msr elr_el2,x0
 mov x0,#0x3c5
 msr spsr_el2,x0
 eret
1:
 adrp x0,boot_stack_top
 add x0,x0,:lo12:boot_stack_top
 mov sp,x0
 adrp x0,__bss_start
 add x0,x0,:lo12:__bss_start
 adrp x1,__bss_end
 add x1,x1,:lo12:__bss_end
2:
 cmp x0,x1
 b.hs 3f
 str xzr,[x0],#8
 b 2b
3:
 mov x0,#0x300000
 msr cpacr_el1,x0
 mov x0,#1
 msr icc_sre_el1,x0
 isb
 adrp x0,test_vectors
 add x0,x0,:lo12:test_vectors
 msr vbar_el1,x0
 isb
 bl qemu_main
 b .
.balign 2048
.global test_vectors
test_vectors:
.rept 8
 b aa64_exception_common
 .space 124
.endr
 b aa64_user_slice_sync_vector
 .space 124
 b aa64_user_slice_irq_vector
 .space 124
 b aa64_exception_common
 .space 124
 b aa64_exception_common
 .space 124
.rept 4
 b aa64_exception_common
 .space 124
.endr
.section .bss,"aw",%nobits
.balign 16
boot_stack:
 // Kernel fixtures keep multiple full region tables on this test-only stack.
 .space 524288
boot_stack_top:
