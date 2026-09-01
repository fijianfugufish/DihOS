/* First EL0 renderer-service checkpoint.  This is deliberately not a Mesa
 * implementation: it only proves the signed static-PIE + supervisor path
 * that the future Mesa/Freedreno service will use. */

.global _start
.type _start, %function
_start:
    /* Query the Mesart ABI before exiting.  A missing broker cannot fake a
     * successful service launch because it returns ENOSYS instead. */
    mov x0, #1
    mov x8, #1
    svc #0
    mov x9, #1
    cmp x0, x9
    b.ne 1f
    movz x10, #0x0001
    movk x10, #0x0705, lsl #16
    cmp x1, x10
    b.ne 1f
    mov x9, #31
    cmp x2, x9
    b.ne 1f
    cbz x3, 1f
    mov x9, #4096
    cmp x5, x9
    b.ne 1f
    movz x11, #0x5254
    movk x11, #0x4d53, lsl #16
    mov x13, x3
    mov x14, x4
    mov x0, #2
    mov x1, x13
    mov x8, #1
    svc #0
    cbnz x0, 1f
    cmp x1, x14
    b.ne 1f
    mov x9, #4096
    cmp x2, x9
    b.ne 1f
    str w11, [x1]
    ldr w12, [x1]
    cmp w11, w12
    b.ne 1f
    /* Obtain the writable command source, capture a sealed private copy,
     * then queue that copy.  Queueing remains scheduler-only in this test. */
    mov x0, #3
    mov x8, #1
    svc #0
    cbnz x0, 1f
    cbz x1, 1f
    cbz x2, 1f
    mov x9, #16384
    cmp x3, x9
    b.ne 1f
    /* Persist the opaque handle in the verified bootstrap page across both
     * service calls instead of relying on a scratch-register lifetime. */
    str w1, [x14, #4]
    /* Exact A7xx Type-7 CP_NOP header: opcode 0x10, zero payload, correct
     * parity bits.  The kernel validator accepts this and nothing broader. */
    movz w11, #0x8000
    movk w11, #0x7010, lsl #16
    str w11, [x2]
    mov x0, #4
    ldr w1, [x14, #4]
    mov x2, #1
    mov x8, #1
    svc #0
    cbnz x0, 1f
    /* Queue the sealed snapshot.  The self-test completes this scheduler
     * fence in the kernel; this is still not a CP hardware submission. */
    mov x0, #5
    ldr w1, [x14, #4]
    mov x8, #1
    svc #0
    cbnz x0, 1f
    cbz x1, 1f
    /* x0 = "MSRT"; x8 = DIHOS_EL0_SYSCALL_EXIT; svc #0 */
    movz x0, #0x5254
    movk x0, #0x4d53, lsl #16
    mov x8, xzr
    svc #0
1:  movz x0, #0x494c
    movk x0, #0x4641, lsl #16 /* "FAIL" */
    mov x8, xzr
    svc #0
2:  b 2b
.size _start, . - _start
