/* i2c1_gsi_rw_probe_once.c */
#include "hardware_probes/i2c1_gsi_rw_probe_once.h"
#include "terminal/terminal_api.h"
#include "asm/asm.h"
#include <stdint.h>
#include <stddef.h>

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

static inline void io_barrier(void)
{
    asm_mmio_barrier();
}

static void dump_range(const char *tag, uint32_t start, uint32_t end, uint32_t *out)
{
    uint32_t off, i = 0;
    terminal_print(tag);
    terminal_print("\n");

    for (off = start; off < end; off += 4u, i++)
    {
        out[i] = rd32(off);
        terminal_print("r off:");
        terminal_print_hex32(off);
        terminal_print(" val:");
        terminal_print_hex32(out[i]);
        terminal_print("\n");
    }
}

static void diff_range(const char *tag, uint32_t start, uint32_t end,
                       const uint32_t *before, const uint32_t *after)
{
    uint32_t off, i = 0;
    terminal_print(tag);
    terminal_print("\n");

    for (off = start; off < end; off += 4u, i++)
    {
        if (before[i] != after[i])
        {
            terminal_print("chg off:");
            terminal_print_hex32(off);
            terminal_print(" before:");
            terminal_print_hex32(before[i]);
            terminal_print(" after:");
            terminal_print_hex32(after[i]);
            terminal_print("\n");
        }
    }
}

static void tiny_delay(void)
{
    volatile uint32_t i;
    for (i = 0; i < 100000u; i++)
        asm_relax();
}

/* intentionally fake-op place holders: replace once real reg meanings are known */
static void try_minimal_nop_sequence(void)
{
    uint32_t v;

    v = rd32(0x060);
    wr32(0x060, v | 1u);
    io_barrier();
    tiny_delay();

    wr32(0x024, rd32(0x024));
    wr32(0x028, rd32(0x028));
    wr32(0x048, rd32(0x048));
    io_barrier();
    tiny_delay();
}

void i2c1_gsi_rw_probe_once(void)
{
    uint32_t a0[0x80 / 4], a1[0x40 / 4], a2[0x40 / 4];
    uint32_t b0[0x80 / 4], b1[0x40 / 4], b2[0x40 / 4];

    terminal_print("i2c1 discovery probe start\n");

    dump_range("pre 000-07f", 0x000, 0x080, a0);
    dump_range("pre 100-13f", 0x100, 0x140, a1);
    dump_range("pre 600-63f", 0x600, 0x640, a2);

    try_minimal_nop_sequence();

    dump_range("post 000-07f", 0x000, 0x080, b0);
    dump_range("post 100-13f", 0x100, 0x140, b1);
    dump_range("post 600-63f", 0x600, 0x640, b2);

    diff_range("diff 000-07f", 0x000, 0x080, a0, b0);
    diff_range("diff 100-13f", 0x100, 0x140, a1, b1);
    diff_range("diff 600-63f", 0x600, 0x640, a2, b2);

    terminal_print("i2c1 discovery probe end\n");
    terminal_flush_log();
}
