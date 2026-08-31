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
    cmp x2, x9
    b.ne 1f
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
