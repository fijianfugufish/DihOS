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
    mov x9, #1023
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
    /* Exercise the freestanding Mesart allocator through its own ABI call.
     * x13/x14 hold the broker-owned bootstrap descriptor across this normal
     * AAPCS64 function call. */
    stp x13, x14, [sp, #-16]!
    bl mesart_runtime_selftest
    ldp x13, x14, [sp], #16
    cbnz x0, .Lfail_runtime
    /* Exercise the first DihOS replacement for Freedreno's DRM-facing
     * device/BO layer.  It is deliberately broker-only: no fd, ioctl, or
     * arbitrary GPU mapping enters the renderer ABI. */
    stp x13, x14, [sp, #-16]!
    bl mesart_fd_winsys_selftest
    ldp x13, x14, [sp], #16
    cbnz x0, .Lfail_winsys
    /* This links and executes upstream Mesa's generated Freedreno device
     * table.  It must resolve the brokered chip ID as the X1-85 A7xx before
     * later Mesa compiler or command-stream code is allowed to run. */
    stp x13, x14, [sp, #-16]!
    bl mesart_fd_mesa_device_selftest
    ldp x13, x14, [sp], #16
    cbnz x0, .Lfail_device_table
    /* Mesa's generated PM4 header also has to agree on the two exact A7xx
     * packet forms exposed by the broker. */
    stp x13, x14, [sp, #-16]!
    bl mesart_fd_mesa_pm4_selftest
    ldp x13, x14, [sp], #16
    cbnz x0, .Lfail_pm4
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
    cbz x3, 1f
    str w11, [x1]
    ldr w12, [x1]
    cmp w11, w12
    b.ne 1f
    /* The runtime heap is pre-mapped during verified service admission.  It
     * is the allocation arena future Mesa code will use; this checks both
     * the broker contract and ordinary EL0 read/write access without adding
     * a general process or Unix syscall ABI. */
    mov x0, #6
    mov x8, #1
    svc #0
    cbnz x0, 1f
    cbz x1, 1f
    movz x9, #0x80, lsl #16
    cmp x2, x9
    b.ne 1f
    mov x9, #1
    cmp x3, x9
    b.ne 1f
    str w11, [x1]
    ldr w12, [x1]
    cmp w11, w12
    b.ne 1f
    /* Submit one CP_MEM_WRITE through the Mesart Freedreno winsys BO and
     * primary-submit objects.  This is the first non-NOP EL0 batch, still
     * checked twice by the Mesart broker and kernel hardware backend. */
    stp x13, x14, [sp, #-16]!
    bl mesart_fd_submit_resource_write_selftest
    ldp x13, x14, [sp], #16
    cbnz x0, 1f
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

/* The normal renderer never reaches these.  They make a signed boot-log
 * failure attributable without giving EL0 any diagnostic syscall surface:
 * F001 runtime foundation, F002 winsys, F003 Mesa device table, F004 PM4. */
.Lfail_runtime:
    movz x0, #0xf001
    b .Lexit_checkpoint_failure
.Lfail_winsys:
    movz x0, #0xf002
    b .Lexit_checkpoint_failure
.Lfail_device_table:
    /* F301 means the broker identity was unavailable; F302..F309 identify
     * the Mesa table result or source-derived X1-85 field it rejected. */
    movz w1, #0xf300
    orr w0, w0, w1
    b .Lexit_checkpoint_failure
.Lfail_pm4:
    movz x0, #0xf004
.Lexit_checkpoint_failure:
    mov x8, xzr
    svc #0
    b .Lexit_checkpoint_failure
.size _start, . - _start
