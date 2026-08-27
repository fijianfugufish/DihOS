#include "asm/asm.h"
#include "terminal/terminal_api.h"
#include "kwrappers/kgfx.h"
#include "system/kcrash_map.h"
#include "bootinfo.h"
#include <stdint.h>

#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)

volatile uint64_t g_aa64_probe_active = 0u;
volatile uint64_t g_aa64_probe_faulted = 0u;
volatile uint64_t g_aa64_probe_resume_elr = 0u;
volatile uint64_t g_aa64_probe_last_esr = 0u;
volatile uint64_t g_aa64_probe_last_far = 0u;
volatile uint64_t g_aa64_probe_last_elr = 0u;
static volatile uint32_t g_aa64_probe_trace = 0u;
void aa64_exception_panic(void) __attribute__((noreturn));
static volatile uint32_t g_aa64_panicking;
static const kfont *g_aa64_panic_font;
typedef struct { uint64_t x[31], esr, far, elr, spsr; } aa64_fault_frame;

static const char *aa64_reason(uint64_t esr)
{
    switch ((esr >> 26) & 0x3fu) { case 0x21: return "instruction abort"; case 0x25: return "data abort"; case 0x2f: return "serror"; default: return "unhandled exception"; }
}
static void aa64_hex(char out[19], uint64_t v)
{
    static const char h[] = "0123456789abcdef"; out[0]='0'; out[1]='x';
    for (uint32_t i=0;i<16;i++) out[i+2]=h[(v >> ((15u-i)*4u)) & 15u]; out[18]=0;
}
static void aa64_terminal_source(const kcrash_location *source)
{
    char line[16]; uint32_t n = 0u, value = source->line;
    if (!value) line[n++] = '0';
    else { char reverse[16]; uint32_t r = 0u; while (value) { reverse[r++] = (char)('0' + value % 10u); value /= 10u; } while (r) line[n++] = reverse[--r]; }
    line[n] = 0;
    terminal_print_inline("function: "); terminal_print(source->function);
    terminal_print_inline("source: "); terminal_print(source->file);
    terminal_print_inline("line: "); terminal_print(line);
}
void asm_aa64_panic_renderer_init(const kfont *font) { if (!g_aa64_panicking) g_aa64_panic_font=font; }
void aa64_exception_dispatch(const aa64_fault_frame *f) __attribute__((noreturn));
void aa64_exception_dispatch(const aa64_fault_frame *f)
{
    char h[19]; kcrash_location source = {0};
    kcolor text_white={245,248,255}, pale={210,225,255};
    __asm__ __volatile__("msr daifset, #0xf" ::: "memory");
    if (g_aa64_panicking) aa64_exception_panic(); g_aa64_panicking=1u;
    extern const boot_info *k_bootinfo_ptr;
    int have_source = k_bootinfo_ptr &&
        kcrash_map_lookup(f->elr, k_bootinfo_ptr->kernel_base_phys, &source) == 0;
    kgfx_panic_begin((kcolor){8,45,120});
    if (g_aa64_panic_font) {
        int x = 56;
        ktext_draw_str(g_aa64_panic_font,x,52,"your pc died \xf0\x9f\xa5\x80",text_white,255,26);
        ktext_draw_str(g_aa64_panic_font,x,260,"why? \\\"",text_white,255,14);
        x += (int)ktext_measure_line_px(g_aa64_panic_font,"why? \\\"",14,0);
        ktext_draw_str(g_aa64_panic_font,x,260,aa64_reason(f->esr),text_white,255,14);
        x += (int)ktext_measure_line_px(g_aa64_panic_font,aa64_reason(f->esr),14,0);
        ktext_draw_str(g_aa64_panic_font,x,260,"\\\"",text_white,255,14);
        ktext_draw_str(g_aa64_panic_font,56,310,"you cant do anything to fix this. restart your pc.",pale,255,8);
        ktext_draw_str(g_aa64_panic_font,56,440,"info for nerds",pale,255,6);
        aa64_hex(h,f->esr); ktext_draw_str(g_aa64_panic_font,56,500,"esr_el1",pale,255,4); ktext_draw_str(g_aa64_panic_font,330,500,h,text_white,255,4); ktext_draw_str(g_aa64_panic_font,760,500,"exception syndrome register",pale,255,4);
        aa64_hex(h,f->far); ktext_draw_str(g_aa64_panic_font,56,550,"far_el1",pale,255,4); ktext_draw_str(g_aa64_panic_font,330,550,h,text_white,255,4); ktext_draw_str(g_aa64_panic_font,760,550,"fault address register",pale,255,4);
        aa64_hex(h,f->elr); ktext_draw_str(g_aa64_panic_font,56,600,"elr_el1",pale,255,4); ktext_draw_str(g_aa64_panic_font,330,600,h,text_white,255,4); ktext_draw_str(g_aa64_panic_font,760,600,"exception link register",pale,255,4);
        aa64_hex(h,f->spsr); ktext_draw_str(g_aa64_panic_font,56,650,"spsr_el1",pale,255,4); ktext_draw_str(g_aa64_panic_font,330,650,h,text_white,255,4); ktext_draw_str(g_aa64_panic_font,760,650,"saved program status register",pale,255,4);
        if (have_source) {
            char line[16]; uint32_t n = 0u, value = source.line;
            if (!value) line[n++] = '0';
            else { char reverse[16]; uint32_t r = 0u; while (value) { reverse[r++] = (char)('0' + value % 10u); value /= 10u; } while (r) line[n++] = reverse[--r]; }
            line[n] = 0;
            ktext_draw_str(g_aa64_panic_font,56,730,"function: ",pale,255,4); ktext_draw_str(g_aa64_panic_font,270,730,source.function,text_white,255,4);
            ktext_draw_str(g_aa64_panic_font,56,780,"source: ",pale,255,4); ktext_draw_str(g_aa64_panic_font,210,780,source.file,text_white,255,4);
            ktext_draw_str(g_aa64_panic_font,56,830,"line: ",pale,255,4); ktext_draw_str(g_aa64_panic_font,180,830,line,text_white,255,4);
        } else {
            ktext_draw_str(g_aa64_panic_font,56,730,"source location unavailable",pale,255,4);
        }
    }
    kgfx_panic_present();
    terminal_error("your pc died");
    terminal_error(aa64_reason(f->esr));
    terminal_print_inline("elr_el1="); terminal_print_inline_hex64(f->elr); terminal_print("");
    if (have_source) aa64_terminal_source(&source);
    else terminal_warn("source location unavailable");
    terminal_flush_log(); aa64_exception_panic();
}

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
        "b aa64_exception_common\n");
}

__attribute__((naked)) void aa64_exception_common(void)
{
    __asm__ __volatile__(
        "sub sp, sp, #0x120\n"
        "stp x0,x1,[sp,#0]\n" "stp x2,x3,[sp,#16]\n" "stp x4,x5,[sp,#32]\n" "stp x6,x7,[sp,#48]\n"
        "stp x8,x9,[sp,#64]\n" "stp x10,x11,[sp,#80]\n" "stp x12,x13,[sp,#96]\n" "stp x14,x15,[sp,#112]\n"
        "stp x16,x17,[sp,#128]\n" "stp x18,x19,[sp,#144]\n" "stp x20,x21,[sp,#160]\n" "stp x22,x23,[sp,#176]\n"
        "stp x24,x25,[sp,#192]\n" "stp x26,x27,[sp,#208]\n" "stp x28,x29,[sp,#224]\n" "str x30,[sp,#240]\n"
        "mrs x1,esr_el1\n" "str x1,[sp,#248]\n" "mrs x1,far_el1\n" "str x1,[sp,#256]\n"
        "mrs x1,elr_el1\n" "str x1,[sp,#264]\n" "mrs x1,spsr_el1\n" "str x1,[sp,#272]\n"
        "mov x0,sp\n" "bl aa64_exception_dispatch\n" "b aa64_exception_panic\n");
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

void asm_aa64_set_probe_trace(int enabled)
{
    g_aa64_probe_trace = enabled ? 1u : 0u;
}

int asm_aa64_try_read32(uint64_t addr, uint32_t *out_value)
{
    uint64_t resume_pc = 0u;
    uint64_t old_vbar = 0u;
    uint64_t old_daif = 0u;
    uintptr_t probe_vbar = (uintptr_t)&aa64_vector_table;
    uint32_t value = 0u;

    if (g_aa64_probe_trace)
    {
        terminal_print("[K:EXC] try_read32 enter addr=");
        terminal_print_inline_hex64(addr);
        terminal_flush_log();
    }

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

    if (g_aa64_probe_trace)
    {
        terminal_print("[K:EXC] try_read32 armed");
        terminal_flush_log();
    }

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

    if (g_aa64_probe_trace || g_aa64_probe_faulted)
    {
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
    }

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

    if (g_aa64_probe_trace)
    {
        terminal_print("[K:EXC] try_write32 enter addr=");
        terminal_print_inline_hex64(addr);
        terminal_print(" value=");
        terminal_print_inline_hex64(value);
        terminal_flush_log();
    }

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

    if (g_aa64_probe_trace)
    {
        terminal_print("[K:EXC] try_write32 armed");
        terminal_flush_log();
    }

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

    if (g_aa64_probe_trace || g_aa64_probe_faulted)
    {
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
    }

    (void)resume_pc;
    g_aa64_probe_active = 0u;

    return g_aa64_probe_faulted ? -1 : 0;
}

int asm_aa64_try_hvc(uint32_t immediate,
                     uint64_t *x0,
                     uint64_t *x1,
                     uint64_t *x2,
                     uint64_t *x3,
                     uint64_t *out_esr)
{
    uint64_t old_vbar = 0u;
    uint64_t old_daif = 0u;
    uintptr_t probe_vbar = (uintptr_t)&aa64_vector_table;
    register uint64_t r0 __asm__("x0") = x0 ? *x0 : 0u;
    register uint64_t r1 __asm__("x1") = x1 ? *x1 : 0u;
    register uint64_t r2 __asm__("x2") = x2 ? *x2 : 0u;
    register uint64_t r3 __asm__("x3") = x3 ? *x3 : 0u;

    g_aa64_probe_faulted = 0u;
    g_aa64_probe_active = 1u;
    g_aa64_probe_last_esr = 0u;

    __asm__ __volatile__(
        "mrs %0, vbar_el1\n"
        "mrs %1, daif\n"
        "msr daifset, #0xf\n"
        "msr vbar_el1, %2\n"
        "isb\n"
        : "=&r"(old_vbar), "=&r"(old_daif)
        : "r"(probe_vbar)
        : "memory");

    if (immediate == 0u)
    {
        __asm__ __volatile__(
            "adr x9, 1f\n"
            "adrp x10, g_aa64_probe_resume_elr\n"
            "add x10, x10, :lo12:g_aa64_probe_resume_elr\n"
            "str x9, [x10]\n"
            "hvc #0\n"
            "1:\n"
            : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
            :
            : "x9", "x10", "memory");
    }
    else
    {
        __asm__ __volatile__(
            "adr x9, 1f\n"
            "adrp x10, g_aa64_probe_resume_elr\n"
            "add x10, x10, :lo12:g_aa64_probe_resume_elr\n"
            "str x9, [x10]\n"
            "hvc #1\n"
            "1:\n"
            : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
            :
            : "x9", "x10", "memory");
    }

    __asm__ __volatile__(
        "msr vbar_el1, %0\n"
        "msr daif, %1\n"
        "isb\n"
        :
        : "r"(old_vbar), "r"(old_daif)
        : "memory");

    g_aa64_probe_active = 0u;
    if (x0)
        *x0 = r0;
    if (x1)
        *x1 = r1;
    if (x2)
        *x2 = r2;
    if (x3)
        *x3 = r3;
    if (out_esr)
        *out_esr = g_aa64_probe_last_esr;
    return g_aa64_probe_faulted ? -1 : 0;
}

int asm_aa64_try_smc(uint32_t immediate,
                     uint64_t *x0,
                     uint64_t *x1,
                     uint64_t *x2,
                     uint64_t *x3,
                     uint64_t *out_esr)
{
    uint64_t old_vbar = 0u;
    uint64_t old_daif = 0u;
    uintptr_t probe_vbar = (uintptr_t)&aa64_vector_table;
    register uint64_t r0 __asm__("x0") = x0 ? *x0 : 0u;
    register uint64_t r1 __asm__("x1") = x1 ? *x1 : 0u;
    register uint64_t r2 __asm__("x2") = x2 ? *x2 : 0u;
    register uint64_t r3 __asm__("x3") = x3 ? *x3 : 0u;

    g_aa64_probe_faulted = 0u;
    g_aa64_probe_active = 1u;
    g_aa64_probe_last_esr = 0u;

    __asm__ __volatile__(
        "mrs %0, vbar_el1\n"
        "mrs %1, daif\n"
        "msr daifset, #0xf\n"
        "msr vbar_el1, %2\n"
        "isb\n"
        : "=&r"(old_vbar), "=&r"(old_daif)
        : "r"(probe_vbar)
        : "memory");

    if (immediate == 0u)
    {
        __asm__ __volatile__(
            "adr x9, 1f\n"
            "adrp x10, g_aa64_probe_resume_elr\n"
            "add x10, x10, :lo12:g_aa64_probe_resume_elr\n"
            "str x9, [x10]\n"
            "smc #0\n"
            "1:\n"
            : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
            :
            : "x9", "x10", "memory");
    }
    else
    {
        __asm__ __volatile__(
            "adr x9, 1f\n"
            "adrp x10, g_aa64_probe_resume_elr\n"
            "add x10, x10, :lo12:g_aa64_probe_resume_elr\n"
            "str x9, [x10]\n"
            "smc #1\n"
            "1:\n"
            : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
            :
            : "x9", "x10", "memory");
    }

    __asm__ __volatile__(
        "msr vbar_el1, %0\n"
        "msr daif, %1\n"
        "isb\n"
        :
        : "r"(old_vbar), "r"(old_daif)
        : "memory");

    g_aa64_probe_active = 0u;
    if (x0)
        *x0 = r0;
    if (x1)
        *x1 = r1;
    if (x2)
        *x2 = r2;
    if (x3)
        *x3 = r3;
    if (out_esr)
        *out_esr = g_aa64_probe_last_esr;
    return g_aa64_probe_faulted ? -1 : 0;
}

int asm_aa64_try_hv_set_vpreg(uint32_t reg,
                              uint64_t value,
                              uint64_t *out_status,
                              uint64_t *out_esr)
{
    uint64_t old_vbar = 0u;
    uint64_t old_daif = 0u;
    uintptr_t probe_vbar = (uintptr_t)&aa64_vector_table;
    register uint64_t x0 __asm__("x0") = 0x46000001u;
    register uint64_t x1 __asm__("x1") =
        0x0051u | (1ull << 16) | (1ull << 32);
    register uint64_t x2 __asm__("x2") = ~0ull;
    register uint64_t x3 __asm__("x3") = 0xFFFFFFFEu;
    register uint64_t x4 __asm__("x4") = reg;
    register uint64_t x5 __asm__("x5") = 0u;
    register uint64_t x6 __asm__("x6") = value;
    register uint64_t x7 __asm__("x7") = 0u;

    g_aa64_probe_faulted = 0u;
    g_aa64_probe_active = 1u;
    g_aa64_probe_last_esr = 0u;

    __asm__ __volatile__(
        "mrs %0, vbar_el1\n"
        "mrs %1, daif\n"
        "msr daifset, #0xf\n"
        "msr vbar_el1, %2\n"
        "isb\n"
        : "=&r"(old_vbar), "=&r"(old_daif)
        : "r"(probe_vbar)
        : "memory");

    __asm__ __volatile__(
        "adr x9, 1f\n"
        "adrp x10, g_aa64_probe_resume_elr\n"
        "add x10, x10, :lo12:g_aa64_probe_resume_elr\n"
        "str x9, [x10]\n"
        "hvc #0\n"
        "1:\n"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
          "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
        :
        : "x9", "x10", "memory");

    __asm__ __volatile__(
        "msr vbar_el1, %0\n"
        "msr daif, %1\n"
        "isb\n"
        :
        : "r"(old_vbar), "r"(old_daif)
        : "memory");

    g_aa64_probe_active = 0u;
    if (out_status)
        *out_status = x0;
    if (out_esr)
        *out_esr = g_aa64_probe_last_esr;
    return g_aa64_probe_faulted ? -1 : 0;
}

#else

void asm_aa64_install_exception_vectors(void) {}
void asm_aa64_set_probe_trace(int enabled)
{
    (void)enabled;
}

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

int asm_aa64_try_hvc(uint32_t immediate,
                     uint64_t *x0,
                     uint64_t *x1,
                     uint64_t *x2,
                     uint64_t *x3,
                     uint64_t *out_esr)
{
    (void)immediate;
    (void)x0;
    (void)x1;
    (void)x2;
    (void)x3;
    if (out_esr)
        *out_esr = 0u;
    return -1;
}

int asm_aa64_try_smc(uint32_t immediate,
                     uint64_t *x0,
                     uint64_t *x1,
                     uint64_t *x2,
                     uint64_t *x3,
                     uint64_t *out_esr)
{
    (void)immediate;
    (void)x0;
    (void)x1;
    (void)x2;
    (void)x3;
    if (out_esr)
        *out_esr = 0u;
    return -1;
}

int asm_aa64_try_hv_set_vpreg(uint32_t reg,
                              uint64_t value,
                              uint64_t *out_status,
                              uint64_t *out_esr)
{
    (void)reg;
    (void)value;
    if (out_status)
        *out_status = ~0ull;
    if (out_esr)
        *out_esr = 0u;
    return -1;
}

#endif
