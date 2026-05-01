#include "hardware_probes/i2c1_delta_probe.h"
#include "terminal/terminal_api.h"
#include "asm/asm.h"
#include <stdint.h>
#include <stddef.h>

#define I2C1_BASE 0x00B80000u

/* GENI / SE common */
#define GENI_OUTPUT_CTRL 0x024
#define GENI_CGC_CTRL 0x028
#define SE_GENI_STATUS 0x040
#define GENI_SER_M_CLK_CFG 0x048
#define GENI_CLK_CTRL_RO 0x060
#define GENI_IF_FIFO_DISABLE_RO 0x064
#define GENI_FW_REVISION_RO 0x068
#define GENI_FW_S_REVISION_RO 0x06C
#define SE_GENI_CLK_SEL 0x07C

/* I2C-specific */
#define SE_I2C_TX_TRANS_LEN 0x26C
#define SE_I2C_RX_TRANS_LEN 0x270
#define SE_I2C_SCL_COUNTERS 0x278

/* GENI sequencer */
#define SE_GENI_DMA_MODE_EN 0x258
#define SE_GENI_M_CMD0 0x600
#define SE_GENI_M_CMD_CTRL_REG 0x604
#define SE_GENI_M_IRQ_STATUS 0x610
#define SE_GENI_M_IRQ_EN 0x614
#define SE_GENI_M_IRQ_CLEAR 0x618
#define SE_GENI_TX_FIFO_STATUS 0x800
#define SE_GENI_RX_FIFO_STATUS 0x804
#define SE_GENI_IOS 0x908

/* Bit defs */
#define SER_CLK_EN (1u << 0)
#define CLK_DIV_SHFT 4

#define M_GENI_CMD_ACTIVE (1u << 0)

#define M_CMD_DONE_EN (1u << 0)
#define M_CMD_OVERRUN_EN (1u << 1)
#define M_ILLEGAL_CMD_EN (1u << 2)
#define M_CMD_FAILURE_EN (1u << 3)
#define M_CMD_CANCEL_EN (1u << 4)
#define M_CMD_ABORT_EN (1u << 5)
#define M_GP_IRQ_0_EN (1u << 9)
#define M_GP_IRQ_1_EN (1u << 10)
#define M_GP_IRQ_2_EN (1u << 11)
#define M_GP_IRQ_3_EN (1u << 12)
#define M_GP_IRQ_4_EN (1u << 13)
#define M_GP_IRQ_5_EN (1u << 14)

#define M_GENI_CMD_ABORT (1u << 1)

/* I2C opcodes */
#define I2C_WRITE 0x1u
#define I2C_READ 0x2u
#define I2C_WRITE_READ 0x3u
#define I2C_ADDR_ONLY 0x4u

#define M_OPCODE_SHFT 27
#define SLV_ADDR_SHFT 9

static inline uint32_t rd32(uint32_t off)
{
    volatile const uint32_t *p =
        (volatile const uint32_t *)(uintptr_t)(I2C1_BASE + off);
    return *p;
}

static inline void wr32(uint32_t off, uint32_t v)
{
    volatile uint32_t *p =
        (volatile uint32_t *)(uintptr_t)(I2C1_BASE + off);
    *p = v;
}

static inline void io_barrier(void)
{
    asm_mmio_barrier();
}

static void tiny_delay(uint32_t n)
{
    volatile uint32_t i;
    for (i = 0; i < n; ++i)
        asm_relax();
}

static void print_reg(const char *name, uint32_t off)
{
    terminal_print(name);
    terminal_print(":");
    terminal_print_hex32(rd32(off));
    terminal_print("\n");
}

static void print_irq_decode(uint32_t v)
{
    terminal_print("irq:");
    terminal_print_hex32(v);
    terminal_print(" [");

    if (v & M_CMD_DONE_EN)
        terminal_print("DONE ");
    if (v & M_CMD_OVERRUN_EN)
        terminal_print("OVERRUN ");
    if (v & M_ILLEGAL_CMD_EN)
        terminal_print("ILLEGAL ");
    if (v & M_CMD_FAILURE_EN)
        terminal_print("FAIL ");
    if (v & M_CMD_CANCEL_EN)
        terminal_print("CANCEL ");
    if (v & M_CMD_ABORT_EN)
        terminal_print("ABORT ");
    if (v & M_GP_IRQ_0_EN)
        terminal_print("GP0 ");
    if (v & M_GP_IRQ_1_EN)
        terminal_print("GP1/NACK ");
    if (v & M_GP_IRQ_2_EN)
        terminal_print("GP2 ");
    if (v & M_GP_IRQ_3_EN)
        terminal_print("GP3/BUS ");
    if (v & M_GP_IRQ_4_EN)
        terminal_print("GP4/ARB ");
    if (v & M_GP_IRQ_5_EN)
        terminal_print("GP5 ");

    terminal_print("]\n");
}

static void geni_i2c_basic_config(void)
{
    /* keep the simple values your logs already showed as alive */
    wr32(GENI_OUTPUT_CTRL, 0x0000007Fu);
    wr32(GENI_CGC_CTRL, 0x0000007Fu);

    /* FIFO mode, not DMA mode */
    wr32(SE_GENI_DMA_MODE_EN, 0x00000000u);

    /* 100 kHz timing from qcom geni i2c driver */
    wr32(SE_GENI_CLK_SEL, 0x00000000u);
    wr32(GENI_SER_M_CLK_CFG, (7u << CLK_DIV_SHFT) | SER_CLK_EN);
    wr32(SE_I2C_SCL_COUNTERS, (10u << 20) | (11u << 10) | 26u);

    /* no payload for ADDR_ONLY */
    wr32(SE_I2C_TX_TRANS_LEN, 0u);
    wr32(SE_I2C_RX_TRANS_LEN, 0u);

    /* clear stale main IRQs */
    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);

    io_barrier();
    tiny_delay(40000u);
}

static void print_compact_state(const char *tag)
{
    terminal_print(tag);
    terminal_print("\n");
    print_reg("st40", SE_GENI_STATUS);
    print_reg("clk48", GENI_SER_M_CLK_CFG);
    print_reg("ro60", GENI_CLK_CTRL_RO);
    print_reg("if64", GENI_IF_FIFO_DISABLE_RO);
    print_reg("fw68", GENI_FW_REVISION_RO);
    print_reg("fs6c", GENI_FW_S_REVISION_RO);
    print_reg("m600", SE_GENI_M_CMD0);
    print_reg("i610", SE_GENI_M_IRQ_STATUS);
    print_reg("tx800", SE_GENI_TX_FIFO_STATUS);
    print_reg("rx804", SE_GENI_RX_FIFO_STATUS);
    print_reg("ios908", SE_GENI_IOS);
}

static void probe_addr_only(uint8_t addr7, const char *name)
{
    uint32_t cmd;
    uint32_t irq;
    uint32_t status;
    uint32_t spins = 0;

    terminal_print("probe ");
    terminal_print(name);
    terminal_print(" addr:");
    terminal_print_hex8(addr7);
    terminal_print("\n");

    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();

    cmd = (I2C_ADDR_ONLY << M_OPCODE_SHFT) |
          ((uint32_t)addr7 << SLV_ADDR_SHFT);

    wr32(SE_GENI_M_CMD0, cmd);
    io_barrier();

    for (;;)
    {
        irq = rd32(SE_GENI_M_IRQ_STATUS);
        status = rd32(SE_GENI_STATUS);

        if (irq & (M_CMD_DONE_EN |
                   M_CMD_FAILURE_EN |
                   M_CMD_ABORT_EN |
                   M_GP_IRQ_0_EN |
                   M_GP_IRQ_1_EN |
                   M_GP_IRQ_2_EN |
                   M_GP_IRQ_3_EN |
                   M_GP_IRQ_4_EN |
                   M_GP_IRQ_5_EN))
            break;

        if ((status & M_GENI_CMD_ACTIVE) == 0u && irq != 0u)
            break;

        if (++spins >= 200000u)
            break;
    }

    terminal_print("cmd:");
    terminal_print_hex32(cmd);
    terminal_print("\n");

    terminal_print("spins:");
    terminal_print_hex32(spins);
    terminal_print("\n");

    print_irq_decode(irq);

    terminal_print("st:");
    terminal_print_hex32(status);
    terminal_print("\n");

    terminal_print("tx:");
    terminal_print_hex32(rd32(SE_GENI_TX_FIFO_STATUS));
    terminal_print(" rx:");
    terminal_print_hex32(rd32(SE_GENI_RX_FIFO_STATUS));
    terminal_print(" ios:");
    terminal_print_hex32(rd32(SE_GENI_IOS));
    terminal_print("\n");

    /* if stuck active, try abort so the next probe starts clean */
    if ((status & M_GENI_CMD_ACTIVE) != 0u &&
        (irq & (M_CMD_DONE_EN | M_CMD_FAILURE_EN | M_CMD_ABORT_EN)) == 0u)
    {
        terminal_warn("stuck active, aborting");
        wr32(SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
        io_barrier();
        tiny_delay(40000u);

        irq = rd32(SE_GENI_M_IRQ_STATUS);
        status = rd32(SE_GENI_STATUS);

        terminal_print("post-abort irq:");
        terminal_print_hex32(irq);
        terminal_print(" st:");
        terminal_print_hex32(status);
        terminal_print("\n");
    }

    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();
    tiny_delay(10000u);
}

void i2c1_delta_probe2(void)
{
    terminal_print("i2c1 probe 2 start\n");
    terminal_print("base:");
    terminal_print_hex32(I2C1_BASE);
    terminal_print("\n");

    geni_i2c_basic_config();
    print_compact_state("baseline");

    if (rd32(GENI_IF_FIFO_DISABLE_RO) & 1u)
    {
        terminal_error("fifo disabled -> gpi/dma only");
        terminal_flush_log();
        return;
    }

    probe_addr_only(0x3Au, "ECKB");
    probe_addr_only(0x2Cu, "TCPD");

    terminal_print("i2c1 probe 2 end\n");
    terminal_flush_log();
}
