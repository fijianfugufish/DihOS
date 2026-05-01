/* i2c1_gsi_activate_once.c */
#include "hardware_probes/i2c1_gsi_activate_once.h"
#include "terminal/terminal_api.h"
#include "asm/asm.h"

#define I2C1_MMIO_BASE 0x00B80000u

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

static void log_reg(const char *tag, uint32_t off)
{
    terminal_print(tag);
    terminal_print(" off:");
    terminal_print_hex32(off);
    terminal_print(" val:");
    terminal_print_hex32(rd32(off));
}

void i2c1_gsi_activate_once(void)
{
    uint32_t r24, r28, r48, r60, r68;

    terminal_print("i2c1 activate start");

    r24 = rd32(0x024);
    r28 = rd32(0x028);
    r48 = rd32(0x048);
    r60 = rd32(0x060);
    r68 = rd32(0x068);

    log_reg("before", 0x024);
    log_reg("before", 0x028);
    log_reg("before", 0x048);
    log_reg("before", 0x060);
    log_reg("before", 0x068);

    /* Do not force 0x060 if already enabled-looking. */
    if ((r60 & 1u) == 0u)
    {
        terminal_print("set enable bit on 0x060");
        wr32(0x060, r60 | 1u);
        mmio_barrier();
    }
    else
    {
        terminal_print("0x060 already bit0=1");
    }

    /* Very cautious status-ack experiment. */
    terminal_print("echo-write 0x024");
    wr32(0x024, r24);
    mmio_barrier();

    log_reg("after", 0x024);
    log_reg("after", 0x028);
    log_reg("after", 0x048);
    log_reg("after", 0x060);
    log_reg("after", 0x068);

    terminal_print("i2c1 activate end");
    terminal_flush_log();
}
