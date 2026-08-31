#include "asm/aa64_user.h"

#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)

#include "asm/asm.h"
#include "memory/pmem.h"
#include "terminal/terminal_api.h"

#define AA64_USER_CONTEXT_MAX_CORES 16u
#define AA64_ESR_EC_SHIFT 26u
#define AA64_ESR_EC_SVC64 0x15u

/* Assembly below relies on this exact all-u64 layout. */
typedef struct aa64_user_context
{
    uint64_t kernel_ttbr0;       /* 0 */
    uint64_t exit_status_out;    /* 8 */
    uint64_t exit_status;        /* 16 */
    uint64_t active;             /* 24 */
    uint64_t kernel_spsr;        /* 32 */
    uint64_t saved_x18_to_x30[13]; /* 40 .. 136 */
    uint64_t syscall_handler;    /* 144 */
    uint64_t syscall_context;    /* 152 */
} aa64_user_context;

static aa64_user_context g_aa64_user_contexts[AA64_USER_CONTEXT_MAX_CORES]
    __attribute__((aligned(16)));

static uint32_t aa64_user_core_index(void)
{
    uint64_t mpidr;

    __asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(mpidr));
    return (uint32_t)(mpidr & 0x0fu);
}

static void aa64_write_ttbr0(uint64_t value)
{
    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("msr ttbr0_el1, %0" :: "r"(value) : "memory");
    __asm__ __volatile__("tlbi vmalle1" ::: "memory");
    __asm__ __volatile__("dsb ish; isb" ::: "memory");
}

void aa64_user_return_trampoline(void) __attribute__((naked));
void aa64_user_return_trampoline(void)
{
    __asm__ __volatile__(
        "mrs x16, mpidr_el1\n"
        "and x16, x16, #0xf\n"
        "adrp x17, g_aa64_user_contexts\n"
        "add x17, x17, :lo12:g_aa64_user_contexts\n"
        "mov x15, #160\n"
        "madd x17, x16, x15, x17\n"
        "ldr x0, [x17, #8]\n"
        "cbz x0, 1f\n"
        "ldr x1, [x17, #16]\n"
        "str x1, [x0]\n"
        "1:\n"
        "ldp x18, x19, [x17, #40]\n"
        "ldp x20, x21, [x17, #56]\n"
        "ldp x22, x23, [x17, #72]\n"
        "ldp x24, x25, [x17, #88]\n"
        "ldp x26, x27, [x17, #104]\n"
        "ldp x28, x29, [x17, #120]\n"
        "ldr x30, [x17, #136]\n"
        "str xzr, [x17, #24]\n"
        "mov x0, xzr\n"
        "ret\n");
}

int aa64_el0_sync_dispatch(aa64_el0_frame *frame)
{
    aa64_user_context *context;
    uint32_t exception_class;

    if (!frame)
        return 0;
    context = &g_aa64_user_contexts[aa64_user_core_index()];
    if (!context->active)
        return 0;
    exception_class = (uint32_t)((frame->esr >> AA64_ESR_EC_SHIFT) & 0x3fu);
    if (exception_class != AA64_ESR_EC_SVC64)
    {
        context->exit_status = DIHOS_EL0_EXIT_FAULT | frame->esr;
        aa64_write_ttbr0(context->kernel_ttbr0);
        frame->x[0] = 0u;
        frame->elr = (uint64_t)(uintptr_t)&aa64_user_return_trampoline;
        frame->spsr = context->kernel_spsr;
        return 1;
    }
    if (frame->x[8] != DIHOS_EL0_SYSCALL_EXIT)
    {
        aa64_user_syscall_handler handler =
            (aa64_user_syscall_handler)(uintptr_t)context->syscall_handler;

        if (handler && handler(frame,
                               (void *)(uintptr_t)context->syscall_context))
            return 1;
        frame->x[0] = (uint64_t)-38; /* ENOSYS without importing libc. */
        return 1;
    }

    context->exit_status = frame->x[0];
    aa64_write_ttbr0(context->kernel_ttbr0);
    frame->x[0] = 0u;
    frame->elr = (uint64_t)(uintptr_t)&aa64_user_return_trampoline;
    frame->spsr = context->kernel_spsr;
    return 1;
}

static int aa64_user_enter_raw(const aarch64_user_vm *vm, uint64_t entry_va,
                               uint64_t user_stack_top,
                               uint64_t *out_exit_status,
                               aa64_user_syscall_handler handler,
                               void *handler_context)
    __attribute__((naked));
static int aa64_user_enter_raw(const aarch64_user_vm *vm, uint64_t entry_va,
                               uint64_t user_stack_top,
                               uint64_t *out_exit_status,
                               aa64_user_syscall_handler handler,
                               void *handler_context)
{
    __asm__ __volatile__(
        "cbz x0, 9f\n"
        "ldr x12, [x0]\n" /* aarch64_user_vm.root_phys */
        "cbz x12, 9f\n"
        "mrs x9, mpidr_el1\n"
        "and x9, x9, #0xf\n"
        "adrp x10, g_aa64_user_contexts\n"
        "add x10, x10, :lo12:g_aa64_user_contexts\n"
        "mov x11, #160\n"
        "madd x10, x9, x11, x10\n"
        "ldr x11, [x10, #24]\n"
        "cbnz x11, 8f\n"
        "mrs x11, ttbr0_el1\n"
        "str x11, [x10, #0]\n"
        "str x3, [x10, #8]\n"
        "mrs x11, daif\n"
        "mov x14, #0x5\n"
        "orr x11, x11, x14\n"
        "str x11, [x10, #32]\n"
        "str x18, [x10, #40]\n"
        "stp x19, x20, [x10, #48]\n"
        "stp x21, x22, [x10, #64]\n"
        "stp x23, x24, [x10, #80]\n"
        "stp x25, x26, [x10, #96]\n"
        "stp x27, x28, [x10, #112]\n"
        "stp x29, x30, [x10, #128]\n"
        "str x4, [x10, #144]\n"
        "str x5, [x10, #152]\n"
        "mov x11, #1\n"
        "str x11, [x10, #24]\n"
        "dsb ishst\n"
        "msr ttbr0_el1, x12\n"
        "tlbi vmalle1\n"
        "dsb ish\n"
        "isb\n"
        "msr sp_el0, x2\n"
        "msr elr_el1, x1\n"
        "mov x11, #0x3c0\n"
        "msr spsr_el1, x11\n" /* EL0t; IRQ/FIQ/SError/debug masked for v1. */
        "eret\n"
        "8:\n"
        "mov x0, #-2\n"
        "ret\n"
        "9:\n"
        "mov x0, #-1\n"
        "ret\n");
}

int aa64_user_enter_with_handler(const aarch64_user_vm *vm, uint64_t entry_va,
                                 uint64_t user_stack_top,
                                 uint64_t *out_exit_status,
                                 aa64_user_syscall_handler handler,
                                 void *handler_context)
{
    return aa64_user_enter_raw(vm, entry_va, user_stack_top, out_exit_status,
                               handler, handler_context);
}

int aa64_user_enter(const aarch64_user_vm *vm, uint64_t entry_va,
                    uint64_t user_stack_top, uint64_t *out_exit_status)
{
    return aa64_user_enter_with_handler(vm, entry_va, user_stack_top,
                                        out_exit_status, 0, 0);
}

int aa64_user_selftest(uint64_t *out_exit_status)
{
    /* mov x0,#0x4d; mov x8,#0; svc #0; b . */
    static const uint32_t program[] = {
        0xd28009a0u, 0xd2800008u, 0xd4000001u, 0x14000000u,
    };
    aarch64_user_vm vm;
    uint32_t *code = 0;
    void *stack = 0;
    uint64_t status = 0u;
    uint64_t code_va;
    uint64_t stack_va;
    uint64_t code_physical;
    uint64_t stack_physical;
    uint64_t cloned_code_physical = 0u;
    int rc;

    if (!out_exit_status)
        return -1;
    *out_exit_status = 0u;
    code = (uint32_t *)pmem_alloc_pages(1u);
    stack = pmem_alloc_pages(1u);
    if (!code || !stack)
    {
        if (code)
            pmem_free_pages(code, 1u);
        if (stack)
            pmem_free_pages(stack, 1u);
        return -2;
    }
    for (uint32_t i = 0u; i < sizeof(program) / sizeof(program[0]); ++i)
        code[i] = program[i];
    asm_sync_executable_range(code, sizeof(program));
    /* The active EL1 tables may carry a firmware VA->PA offset.  Reuse the
     * backing pages' existing virtual addresses, then replace only those
     * inherited leaves in this VM's private table clone. */
    code_va = (uint64_t)(uintptr_t)code;
    stack_va = (uint64_t)(uintptr_t)stack;
    rc = aarch64_user_vm_translate_current(code_va, &code_physical);
    if (rc == 0)
        rc = aarch64_user_vm_translate_current(stack_va, &stack_physical);
    if (rc != 0)
    {
        pmem_free_pages(code, 1u);
        pmem_free_pages(stack, 1u);
        return -4 + rc;
    }
    rc = aarch64_user_vm_create(&vm);
    if (rc != 0)
    {
        aarch64_user_vm_release(&vm);
        pmem_free_pages(code, 1u);
        pmem_free_pages(stack, 1u);
        return -10 + rc;
    }
    {
        int clone_rc = aarch64_user_vm_translate(&vm, code_va,
                                                  &cloned_code_physical);
        terminal_print("[K:PROC] EL0 test code VA=");
        terminal_print_inline_hex64(code_va);
        terminal_print(" active-PA=");
        terminal_print_inline_hex64(code_physical);
        terminal_print(" clone-PA=");
        terminal_print_inline_hex64(cloned_code_physical);
        terminal_print(" clone-rc=");
        terminal_print_inline_hex64((uint64_t)(uint32_t)(-clone_rc));
        terminal_flush_log();
    }
    rc = aarch64_user_vm_map(&vm, code_va, code_physical,
                             AARCH64_USER_VM_PAGE_SIZE,
                             AARCH64_USER_VM_READ |
                             AARCH64_USER_VM_EXECUTE);
    if (rc != 0)
    {
        aarch64_user_vm_release(&vm);
        pmem_free_pages(code, 1u);
        pmem_free_pages(stack, 1u);
        return -20 + rc;
    }
    rc = aarch64_user_vm_map(&vm, stack_va, stack_physical,
                             AARCH64_USER_VM_PAGE_SIZE,
                             AARCH64_USER_VM_READ |
                             AARCH64_USER_VM_WRITE);
    if (rc != 0)
    {
        aarch64_user_vm_release(&vm);
        pmem_free_pages(code, 1u);
        pmem_free_pages(stack, 1u);
        return -20 + rc;
    }
    rc = aa64_user_enter(&vm, code_va,
                         stack_va + AARCH64_USER_VM_PAGE_SIZE - 16u,
                         &status);
    aarch64_user_vm_release(&vm);
    pmem_free_pages(code, 1u);
    pmem_free_pages(stack, 1u);
    if (rc != 0)
        return rc;
    *out_exit_status = status;
    return status == 0x4du ? 0 : -3;
}

#endif
