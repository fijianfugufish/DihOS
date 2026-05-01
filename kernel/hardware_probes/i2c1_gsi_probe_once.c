/* i2c1_gsi_probe_tiny_once.c */
#include "hardware_probes/i2c1_gsi_probe_once.h"
#include "terminal/terminal_api.h"
#include "asm/asm.h"

#define I2C1_MMIO_BASE 0x00B80000u
#define I2C1_MMIO_SIZE 0x00004000u
#define I2C1_IRQ_NUM 0x00000195u

/* Keep this 0 for now. */
#define I2C1_DO_SOFT_RESET 0

static inline uint32_t rd32(uint32_t off)
{
    volatile const uint32_t *p =
        (volatile const uint32_t *)(uintptr_t)(I2C1_MMIO_BASE + off);
    return *p;
}

static inline void wr32(uint32_t off, uint32_t v)
{
    volatile uint32_t *p =
        (volatile uint32_t *)(uintptr_t)(I2C1_MMIO_BASE + off);
    *p = v;
}

static inline void mmio_barrier(void)
{
    asm_mmio_barrier();
}

static int off_ok(uint32_t off)
{
    return ((off & 3u) == 0u) && (off < I2C1_MMIO_SIZE);
}

static void log_named_reg(const char *name, uint32_t off)
{
    if (!off_ok(off))
    {
        terminal_error("i2c1: bad offset");
        terminal_print(name);
        terminal_print("off:");
        terminal_print_hex32(off);
        return;
    }

    terminal_print(name);
    terminal_print("off:");
    terminal_print_hex32(off);
    terminal_print("val:");
    terminal_print_hex32(rd32(off));
}

static void log_named_reg_repeat(const char *name, uint32_t off)
{
    uint32_t a, b, c;

    if (!off_ok(off))
        return;

    a = rd32(off);
    b = rd32(off);
    c = rd32(off);

    terminal_print(name);
    terminal_print("off:");
    terminal_print_hex32(off);
    terminal_print("a:");
    terminal_print_hex32(a);
    terminal_print("b:");
    terminal_print_hex32(b);
    terminal_print("c:");
    terminal_print_hex32(c);

    if (a == b && b == c)
        terminal_success("stable");
    else
        terminal_warn("changed");
}

static void summary_check(void)
{
    uint32_t r0 = rd32(0x000);
    uint32_t r4 = rd32(0x004);
    uint32_t r10 = rd32(0x010);
    uint32_t r24 = rd32(0x024);
    uint32_t r28 = rd32(0x028);
    uint32_t r48 = rd32(0x048);
    uint32_t r60 = rd32(0x060);
    uint32_t r68 = rd32(0x068);
    uint32_t r100 = rd32(0x100);
    uint32_t r110 = rd32(0x110);
    uint32_t r138 = rd32(0x138);
    uint32_t r600 = rd32(0x600);
    uint32_t r610 = rd32(0x610);
    uint32_t r614 = rd32(0x614);

    terminal_print("i2c1 summary");
    terminal_print("r0:");
    terminal_print_hex32(r0);
    terminal_print("r4:");
    terminal_print_hex32(r4);
    terminal_print("r10:");
    terminal_print_hex32(r10);
    terminal_print("r24:");
    terminal_print_hex32(r24);
    terminal_print("r28:");
    terminal_print_hex32(r28);
    terminal_print("r48:");
    terminal_print_hex32(r48);
    terminal_print("r60:");
    terminal_print_hex32(r60);
    terminal_print("r68:");
    terminal_print_hex32(r68);
    terminal_print("r100:");
    terminal_print_hex32(r100);
    terminal_print("r110:");
    terminal_print_hex32(r110);
    terminal_print("r138:");
    terminal_print_hex32(r138);
    terminal_print("r600:");
    terminal_print_hex32(r600);
    terminal_print("r610:");
    terminal_print_hex32(r610);
    terminal_print("r614:");
    terminal_print_hex32(r614);

    if (r600 == 0u || r600 == 0xFFFFFFFFu)
        terminal_warn("cap/version reg suspicious");
    else
        terminal_success("cap/version reg sane");

    if (r24 == 0xFFFFFFFFu || r28 == 0xFFFFFFFFu)
        terminal_warn("status/mask suspicious");
    else
        terminal_success("status/mask sane");
}

static void repeatability_check(void)
{
    terminal_print("i2c1 repeatability");

    log_named_reg_repeat("rep", 0x000);
    log_named_reg_repeat("rep", 0x004);
    log_named_reg_repeat("rep", 0x010);
    log_named_reg_repeat("rep", 0x024);
    log_named_reg_repeat("rep", 0x028);
    log_named_reg_repeat("rep", 0x048);
    log_named_reg_repeat("rep", 0x060);
    log_named_reg_repeat("rep", 0x068);
    log_named_reg_repeat("rep", 0x100);
    log_named_reg_repeat("rep", 0x110);
    log_named_reg_repeat("rep", 0x138);
    log_named_reg_repeat("rep", 0x600);
    log_named_reg_repeat("rep", 0x610);
    log_named_reg_repeat("rep", 0x614);
}

static void chosen_register_dump(void)
{
    terminal_print("i2c1 chosen regs");

    log_named_reg("reg", 0x000);
    log_named_reg("reg", 0x004);
    log_named_reg("reg", 0x010);
    log_named_reg("reg", 0x024);
    log_named_reg("reg", 0x028);
    log_named_reg("reg", 0x048);
    log_named_reg("reg", 0x060);
    log_named_reg("reg", 0x068);
    log_named_reg("reg", 0x100);
    log_named_reg("reg", 0x110);
    log_named_reg("reg", 0x138);
    log_named_reg("reg", 0x600);
    log_named_reg("reg", 0x610);
    log_named_reg("reg", 0x614);
}

static void maybe_soft_reset(void)
{
#if I2C1_DO_SOFT_RESET
    terminal_warn("i2c1: soft reset enabled");
    mmio_barrier();
#else
    terminal_print("i2c1: soft reset skipped");
#endif
}

void i2c1_gsi_probe_once(void)
{
    terminal_print("i2c1 tiny probe start");

    terminal_print("base:");
    terminal_print_hex32(I2C1_MMIO_BASE);
    terminal_print("size:");
    terminal_print_hex32(I2C1_MMIO_SIZE);
    terminal_print("irq:");
    terminal_print_hex32(I2C1_IRQ_NUM);

    mmio_barrier();

    summary_check();
    repeatability_check();
    chosen_register_dump();
    maybe_soft_reset();

    mmio_barrier();

    terminal_print("i2c1 tiny probe end");

    /* One flush at the very end. No clears in here. */
    terminal_flush_log();
}
