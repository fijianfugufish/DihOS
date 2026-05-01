#include "hardware_probes/i2c1_probe_once.h"
#include "terminal/terminal_api.h"
#include "asm/asm.h"

/*
    One-use raw MMIO probe for your laptop's ACPI-decoded I2C1 controller.

    Hardcoded from your working _CRS decode:
      base = 0x00B80000
      size = 0x00004000
      irq  = 0x00000195

    Assumption:
      Physical MMIO is directly accessible at the same address.
      If not, you must map the region before using this file.
*/

#define I2C1_MMIO_BASE_PHYS 0x00B80000u
#define I2C1_MMIO_SIZE 0x00004000u
#define I2C1_IRQ_NUM 0x00000195u

/* Keep reads aligned and conservative. */
static inline uint32_t mmio_read32(uint32_t off)
{
    volatile const uint32_t *p =
        (volatile const uint32_t *)(uintptr_t)(I2C1_MMIO_BASE_PHYS + off);
    return *p;
}

static inline void mmio_write32(uint32_t off, uint32_t value)
{
    volatile uint32_t *p =
        (volatile uint32_t *)(uintptr_t)(I2C1_MMIO_BASE_PHYS + off);
    *p = value;
}

/* Tiny barrier helpers for ordered MMIO access on ARM. */
static inline void mmio_barrier(void)
{
    asm_mmio_barrier();
}

static int off_ok(uint32_t off)
{
    if ((off & 3u) != 0u)
        return 0;
    if (off >= I2C1_MMIO_SIZE)
        return 0;
    return 1;
}

static void dump_reg32(const char *label, uint32_t off)
{
    uint32_t v;

    if (!off_ok(off))
    {
        terminal_error("i2c1 probe: bad offset");
        terminal_print(label);
        terminal_print("off:");
        terminal_print_hex32(off);
        return;
    }

    v = mmio_read32(off);

    terminal_print(label);
    terminal_print("off:");
    terminal_print_hex32(off);
    terminal_print("val:");
    terminal_print_hex32(v);
}

static void dump_range_dwords(uint32_t start_off, uint32_t bytes)
{
    uint32_t off;
    uint32_t end = start_off + bytes;

    if (end > I2C1_MMIO_SIZE)
        end = I2C1_MMIO_SIZE;

    for (off = start_off; off + 4 <= end; off += 4)
    {
        terminal_print("reg");
        terminal_print("off:");
        terminal_print_hex32(off);
        terminal_print("val:");
        terminal_print_hex32(mmio_read32(off));
    }
}

/*
    These offsets are intentionally generic / low-risk.
    We are not assuming exact register semantics yet.
    We are just checking for a live register block and repeatable values.
*/
static void dump_interesting_offsets(void)
{
    terminal_print("i2c1 probe: interesting offsets");

    dump_reg32("r", 0x000);
    dump_reg32("r", 0x004);
    dump_reg32("r", 0x008);
    dump_reg32("r", 0x00C);

    dump_reg32("r", 0x020);
    dump_reg32("r", 0x024);
    dump_reg32("r", 0x028);
    dump_reg32("r", 0x02C);

    dump_reg32("r", 0x040);
    dump_reg32("r", 0x044);
    dump_reg32("r", 0x048);
    dump_reg32("r", 0x04C);

    dump_reg32("r", 0x100);
    dump_reg32("r", 0x104);
    dump_reg32("r", 0x108);
    dump_reg32("r", 0x10C);

    dump_reg32("r", 0x600);
    dump_reg32("r", 0x604);
    dump_reg32("r", 0x608);
    dump_reg32("r", 0x60C);
}

void i2c1_probe_once(void)
{
    uint32_t a0, a1, b0, b1;

    terminal_print("i2c1 probe once start");

    terminal_print("base:");
    terminal_print_hex32(I2C1_MMIO_BASE_PHYS);

    terminal_print("size:");
    terminal_print_hex32(I2C1_MMIO_SIZE);

    terminal_print("irq:");
    terminal_print_hex32(I2C1_IRQ_NUM);

    /*
        Read the same spots twice.
        This helps distinguish:
        - stable register block
        - reads returning junk / bus faults / changing status bits everywhere
    */
    mmio_barrier();
    a0 = mmio_read32(0x000);
    a1 = mmio_read32(0x004);
    b0 = mmio_read32(0x000);
    b1 = mmio_read32(0x004);
    mmio_barrier();

    terminal_print("double-read check");
    terminal_print("r0 first:");
    terminal_print_hex32(a0);
    terminal_print("r1 first:");
    terminal_print_hex32(a1);
    terminal_print("r0 second:");
    terminal_print_hex32(b0);
    terminal_print("r1 second:");
    terminal_print_hex32(b1);

    if (a0 == b0 && a1 == b1)
        terminal_success("i2c1 probe: stable initial reads");
    else
        terminal_warn("i2c1 probe: initial reads changed");

    terminal_print("i2c1 probe: first 0x40 bytes");
    dump_range_dwords(0x000, 0x40);

    dump_interesting_offsets();

    terminal_print("i2c1 probe once end");

    terminal_flush_log();
}
