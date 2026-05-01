#include "hardware_probes/i2c1_delta_probe.h"
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

static void tiny_delay(void)
{
    volatile uint32_t i;
    for (i = 0; i < 50000u; ++i)
        asm_relax();
}

static void snap_range(uint32_t start, uint32_t end, uint32_t *out)
{
    uint32_t off;
    uint32_t i = 0;

    for (off = start; off < end; off += 4u, ++i)
        out[i] = rd32(off);
}

static void print_diffs(const char *tag,
                        uint32_t start,
                        uint32_t end,
                        const uint32_t *pre,
                        const uint32_t *post)
{
    uint32_t off;
    uint32_t i = 0;
    uint32_t any = 0;

    terminal_print(tag);
    terminal_print("\n");

    for (off = start; off < end; off += 4u, ++i)
    {
        if (pre[i] != post[i])
        {
            any = 1;
            terminal_print("chg off:");
            terminal_print_hex32(off);
            terminal_print(" pre:");
            terminal_print_hex32(pre[i]);
            terminal_print(" post:");
            terminal_print_hex32(post[i]);
            terminal_print("\n");
        }
    }

    if (!any)
        terminal_print("no changes\n");
}

static void try_minimal_nop_sequence(void)
{
    uint32_t v;

    terminal_print("poke seq start\n");

    v = rd32(0x060);
    terminal_print("r 060:");
    terminal_print_hex32(v);
    terminal_print("\n");

    wr32(0x060, v | 1u);
    io_barrier();
    tiny_delay();

    wr32(0x024, rd32(0x024));
    wr32(0x028, rd32(0x028));
    wr32(0x048, rd32(0x048));
    io_barrier();
    tiny_delay();

    terminal_print("poke seq end\n");
}

void i2c1_delta_probe(void)
{
    uint32_t pre0[0x80 / 4];
    uint32_t pre1[0x40 / 4];
    uint32_t pre6[0x40 / 4];
    uint32_t post0[0x80 / 4];
    uint32_t post1[0x40 / 4];
    uint32_t post6[0x40 / 4];

    terminal_print("i2c1 delta probe 1 start\n");
    terminal_print("base:");
    terminal_print_hex32(I2C1_MMIO_BASE);
    terminal_print("\n");

    snap_range(0x000, 0x080, pre0);
    snap_range(0x100, 0x140, pre1);
    snap_range(0x600, 0x640, pre6);

    try_minimal_nop_sequence();

    snap_range(0x000, 0x080, post0);
    snap_range(0x100, 0x140, post1);
    snap_range(0x600, 0x640, post6);

    print_diffs("diff 000-07f", 0x000, 0x080, pre0, post0);
    print_diffs("diff 100-13f", 0x100, 0x140, pre1, post1);
    print_diffs("diff 600-63f", 0x600, 0x640, pre6, post6);

    terminal_print("i2c1 delta probe 1 end\n");
    terminal_flush_log();
}
