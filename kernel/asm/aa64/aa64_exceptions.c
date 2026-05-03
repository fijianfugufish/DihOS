#include "asm/asm.h"
#include "terminal/terminal_api.h"
#include <stdint.h>

#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)

volatile uint64_t g_aa64_probe_active = 0u;
volatile uint64_t g_aa64_probe_faulted = 0u;
volatile uint64_t g_aa64_probe_resume_elr = 0u;
volatile uint64_t g_aa64_probe_last_esr = 0u;
volatile uint64_t g_aa64_probe_last_far = 0u;
volatile uint64_t g_aa64_probe_last_elr = 0u;

void aa64_exception_panic(void)
{
    for (;;)
        __asm__ __volatile__("wfe" ::: "memory");
}

__attribute__((naked)) void aa64_sync_current_el_sp0(void)
{
    __asm__ __volatile__("b aa64_sync_current_el_spx");
}

__attribute__((naked)) void aa64_sync_current_el_spx(void)
{
    __asm__ __volatile__(
        "mrs x0, esr_el1\n"
        "mrs x1, far_el1\n"
        "mrs x2, elr_el1\n"

        "adrp x3, g_aa64_probe_last_esr\n"
        "add  x3, x3, :lo12:g_aa64_probe_last_esr\n"
        "str  x0, [x3]\n"

        "adrp x3, g_aa64_probe_last_far\n"
        "add  x3, x3, :lo12:g_aa64_probe_last_far\n"
        "str  x1, [x3]\n"

        "adrp x3, g_aa64_probe_last_elr\n"
        "add  x3, x3, :lo12:g_aa64_probe_last_elr\n"
        "str  x2, [x3]\n"

        "adrp x5, g_aa64_probe_active\n"
        "add  x5, x5, :lo12:g_aa64_probe_active\n"
        "ldr  x6, [x5]\n"
        "cbz  x6, 9f\n"

        "mov  x6, #1\n"
        "adrp x7, g_aa64_probe_faulted\n"
        "add  x7, x7, :lo12:g_aa64_probe_faulted\n"
        "str  x6, [x7]\n"

        "adrp x7, g_aa64_probe_resume_elr\n"
        "add  x7, x7, :lo12:g_aa64_probe_resume_elr\n"
        "ldr  x6, [x7]\n"
        "msr  elr_el1, x6\n"
        "eret\n"

        "9:\n"
        "b aa64_exception_panic\n");
}

__attribute__((naked)) void aa64_irq_current_el_sp0(void) { __asm__ __volatile__("b aa64_exception_panic"); }
__attribute__((naked)) void aa64_fiq_current_el_sp0(void) { __asm__ __volatile__("b aa64_exception_panic"); }
__attribute__((naked)) void aa64_serr_current_el_sp0(void) { __asm__ __volatile__("b aa64_sync_current_el_spx"); }
__attribute__((naked)) void aa64_irq_current_el_spx(void) { __asm__ __volatile__("b aa64_exception_panic"); }
__attribute__((naked)) void aa64_fiq_current_el_spx(void) { __asm__ __volatile__("b aa64_exception_panic"); }
__attribute__((naked)) void aa64_serr_current_el_spx(void) { __asm__ __volatile__("b aa64_sync_current_el_spx"); }
__attribute__((naked)) void aa64_sync_lower_el_a64(void) { __asm__ __volatile__("b aa64_exception_panic"); }
__attribute__((naked)) void aa64_irq_lower_el_a64(void) { __asm__ __volatile__("b aa64_exception_panic"); }
__attribute__((naked)) void aa64_fiq_lower_el_a64(void) { __asm__ __volatile__("b aa64_exception_panic"); }
__attribute__((naked)) void aa64_serr_lower_el_a64(void) { __asm__ __volatile__("b aa64_exception_panic"); }
__attribute__((naked)) void aa64_sync_lower_el_a32(void) { __asm__ __volatile__("b aa64_exception_panic"); }
__attribute__((naked)) void aa64_irq_lower_el_a32(void) { __asm__ __volatile__("b aa64_exception_panic"); }
__attribute__((naked)) void aa64_fiq_lower_el_a32(void) { __asm__ __volatile__("b aa64_exception_panic"); }
__attribute__((naked)) void aa64_serr_lower_el_a32(void) { __asm__ __volatile__("b aa64_exception_panic"); }

__attribute__((naked, aligned(2048))) static void aa64_vector_table(void)
{
    __asm__ __volatile__(
        "b aa64_sync_current_el_sp0\n"
        ".space 0x80 - 4\n"
        "b aa64_irq_current_el_sp0\n"
        ".space 0x80 - 4\n"
        "b aa64_fiq_current_el_sp0\n"
        ".space 0x80 - 4\n"
        "b aa64_serr_current_el_sp0\n"
        ".space 0x80 - 4\n"

        "b aa64_sync_current_el_spx\n"
        ".space 0x80 - 4\n"
        "b aa64_irq_current_el_spx\n"
        ".space 0x80 - 4\n"
        "b aa64_fiq_current_el_spx\n"
        ".space 0x80 - 4\n"
        "b aa64_serr_current_el_spx\n"
        ".space 0x80 - 4\n"

        "b aa64_sync_lower_el_a64\n"
        ".space 0x80 - 4\n"
        "b aa64_irq_lower_el_a64\n"
        ".space 0x80 - 4\n"
        "b aa64_fiq_lower_el_a64\n"
        ".space 0x80 - 4\n"
        "b aa64_serr_lower_el_a64\n"
        ".space 0x80 - 4\n"

        "b aa64_sync_lower_el_a32\n"
        ".space 0x80 - 4\n"
        "b aa64_irq_lower_el_a32\n"
        ".space 0x80 - 4\n"
        "b aa64_fiq_lower_el_a32\n"
        ".space 0x80 - 4\n"
        "b aa64_serr_lower_el_a32\n"
        ".space 0x80 - 4\n");
}

void asm_aa64_install_exception_vectors(void)
{
    uintptr_t vbar = (uintptr_t)&aa64_vector_table;
    __asm__ __volatile__("msr vbar_el1, %0" ::"r"(vbar) : "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

int asm_aa64_try_read32(uint64_t addr, uint32_t *out_value)
{
    uint64_t resume_pc = 0u;
    uint64_t old_vbar = 0u;
    uint64_t old_daif = 0u;
    uintptr_t probe_vbar = (uintptr_t)&aa64_vector_table;
    uint32_t value = 0u;

    terminal_print("[K:EXC] try_read32 enter addr=");
    terminal_print_inline_hex64(addr);
    terminal_flush_log();

    g_aa64_probe_faulted = 0u;
    g_aa64_probe_active = 1u;

    __asm__ __volatile__(
        "mrs %0, vbar_el1\n"
        "mrs %1, daif\n"
        "msr daifset, #0xf\n"
        "msr vbar_el1, %2\n"
        "isb\n"
        : "=&r"(old_vbar), "=&r"(old_daif)
        : "r"(probe_vbar)
        : "memory");

    terminal_print("[K:EXC] try_read32 armed");
    terminal_flush_log();

    __asm__ __volatile__(
        "adr %0, 1f\n"
        "str %0, [%2]\n"
        "ldr %w1, [%3]\n"
        "1:\n"
        : "=&r"(resume_pc), "=&r"(value)
        : "r"(&g_aa64_probe_resume_elr), "r"(addr)
        : "memory");

    __asm__ __volatile__(
        "msr vbar_el1, %0\n"
        "msr daif, %1\n"
        "isb\n"
        :
        : "r"(old_vbar), "r"(old_daif)
        : "memory");

    terminal_print("[K:EXC] try_read32 restored fault=");
    terminal_print_inline_hex64(g_aa64_probe_faulted);
    if (g_aa64_probe_faulted)
    {
        terminal_print(" esr=");
        terminal_print_inline_hex64(g_aa64_probe_last_esr);
        terminal_print(" far=");
        terminal_print_inline_hex64(g_aa64_probe_last_far);
        terminal_print(" elr=");
        terminal_print_inline_hex64(g_aa64_probe_last_elr);
    }
    terminal_flush_log();

    (void)resume_pc;
    g_aa64_probe_active = 0u;

    if (g_aa64_probe_faulted)
    {
        if (out_value)
            *out_value = 0u;
        return -1;
    }

    if (out_value)
        *out_value = value;
    return 0;
}

int asm_aa64_try_write32(uint64_t addr, uint32_t value)
{
    uint64_t resume_pc = 0u;
    uint64_t old_vbar = 0u;
    uint64_t old_daif = 0u;
    uintptr_t probe_vbar = (uintptr_t)&aa64_vector_table;

    terminal_print("[K:EXC] try_write32 enter addr=");
    terminal_print_inline_hex64(addr);
    terminal_print(" value=");
    terminal_print_inline_hex64(value);
    terminal_flush_log();

    g_aa64_probe_faulted = 0u;
    g_aa64_probe_active = 1u;

    __asm__ __volatile__(
        "mrs %0, vbar_el1\n"
        "mrs %1, daif\n"
        "msr daifset, #0xf\n"
        "msr vbar_el1, %2\n"
        "isb\n"
        : "=&r"(old_vbar), "=&r"(old_daif)
        : "r"(probe_vbar)
        : "memory");

    terminal_print("[K:EXC] try_write32 armed");
    terminal_flush_log();

    __asm__ __volatile__(
        "adr %0, 1f\n"
        "str %0, [%1]\n"
        "str %w2, [%3]\n"
        "1:\n"
        : "=&r"(resume_pc)
        : "r"(&g_aa64_probe_resume_elr), "r"(value), "r"(addr)
        : "memory");

    __asm__ __volatile__(
        "msr vbar_el1, %0\n"
        "msr daif, %1\n"
        "isb\n"
        :
        : "r"(old_vbar), "r"(old_daif)
        : "memory");

    terminal_print("[K:EXC] try_write32 restored fault=");
    terminal_print_inline_hex64(g_aa64_probe_faulted);
    if (g_aa64_probe_faulted)
    {
        terminal_print(" esr=");
        terminal_print_inline_hex64(g_aa64_probe_last_esr);
        terminal_print(" far=");
        terminal_print_inline_hex64(g_aa64_probe_last_far);
        terminal_print(" elr=");
        terminal_print_inline_hex64(g_aa64_probe_last_elr);
    }
    terminal_flush_log();

    (void)resume_pc;
    g_aa64_probe_active = 0u;

    return g_aa64_probe_faulted ? -1 : 0;
}

#else

void asm_aa64_install_exception_vectors(void) {}
int asm_aa64_try_read32(uint64_t addr, uint32_t *out_value)
{
    (void)addr;
    if (out_value)
        *out_value = 0u;
    return -1;
}

int asm_aa64_try_write32(uint64_t addr, uint32_t value)
{
    (void)addr;
    (void)value;
    return -1;
}

#endif
