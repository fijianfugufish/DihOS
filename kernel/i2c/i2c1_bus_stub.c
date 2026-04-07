#include "i2c/i2c1_hidi2c.h"
#include "terminal/terminal_api.h"
#include <stdint.h>
#include <stddef.h>

#define I2C1_BASE 0x00B80000u

/* Common GENI regs */
#define GENI_OUTPUT_CTRL 0x024
#define GENI_CGC_CTRL 0x028
#define SE_GENI_STATUS 0x040
#define GENI_SER_M_CLK_CFG 0x048
#define GENI_CLK_CTRL_RO 0x060
#define GENI_IF_FIFO_DISABLE_RO 0x064
#define GENI_FW_REVISION_RO 0x068
#define SE_GENI_CLK_SEL 0x07C

#define SE_GENI_BYTE_GRAN 0x254
#define SE_GENI_DMA_MODE_EN 0x258
#define SE_GENI_TX_PACKING_CFG0 0x260
#define SE_GENI_TX_PACKING_CFG1 0x264
#define SE_GENI_RX_PACKING_CFG0 0x284
#define SE_GENI_RX_PACKING_CFG1 0x288

/* I2C-specific */
#define SE_I2C_TX_TRANS_LEN 0x26C
#define SE_I2C_RX_TRANS_LEN 0x270
#define SE_I2C_SCL_COUNTERS 0x278

/* Master sequencer */
#define SE_GENI_M_CMD0 0x600
#define SE_GENI_M_CMD_CTRL_REG 0x604
#define SE_GENI_M_IRQ_STATUS 0x610
#define SE_GENI_M_IRQ_EN 0x614
#define SE_GENI_M_IRQ_CLEAR 0x618

/* FIFO */
#define SE_GENI_TX_FIFOn 0x700
#define SE_GENI_RX_FIFOn 0x780
#define SE_GENI_TX_FIFO_STATUS 0x800
#define SE_GENI_RX_FIFO_STATUS 0x804
#define SE_GENI_TX_WATERMARK_REG 0x80C
#define SE_GENI_RX_WATERMARK_REG 0x810

#define SE_GENI_IOS 0x908

/* Bits */
#define BIT(x) (1u << (x))

#define SER_CLK_EN BIT(0)
#define CLK_DIV_SHFT 4

#define M_OPCODE_SHFT 27
#define SLV_ADDR_SHFT 9

#define M_GENI_CMD_ABORT BIT(1)

#define M_CMD_DONE_EN BIT(0)
#define M_CMD_OVERRUN_EN BIT(1)
#define M_ILLEGAL_CMD_EN BIT(2)
#define M_CMD_FAILURE_EN BIT(3)
#define M_CMD_CANCEL_EN BIT(4)
#define M_CMD_ABORT_EN BIT(5)
#define M_GP_IRQ_0_EN BIT(9)
#define M_GP_IRQ_1_EN BIT(10)
#define M_GP_IRQ_2_EN BIT(11)
#define M_GP_IRQ_3_EN BIT(12)
#define M_GP_IRQ_4_EN BIT(13)
#define M_GP_IRQ_5_EN BIT(14)
#define M_RX_FIFO_WATERMARK_EN BIT(26)
#define M_RX_FIFO_LAST_EN BIT(27)
#define M_TX_FIFO_WATERMARK_EN BIT(30)

#define RX_LAST BIT(31)
#define RX_LAST_BYTE_VALID_SHFT 28
#define RX_LAST_BYTE_VALID_MSK (7u << RX_LAST_BYTE_VALID_SHFT)
#define RX_FIFO_WC_MSK 0x01FFFFFFu

#define TX_FIFO_WC_MSK 0x0FFFFFFFu

#define GENI_M_CMD_ACTIVE BIT(0)

/* I2C opcodes */
#define I2C_WRITE 0x1u
#define I2C_READ 0x2u
#define I2C_WRITE_READ 0x3u
#define I2C_ADDR_ONLY 0x4u

/* Conservative timeout */
#define I2C1_SPIN_LIMIT 400000u

#define STOP_STRETCH BIT(2)

static uint8_t g_inited = 0;

static uint8_t g_i2c1_quiet = 0;

void i2c1_bus_set_quiet(uint8_t quiet)
{
    g_i2c1_quiet = quiet;
}

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
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

static void tiny_delay(uint32_t n)
{
    volatile uint32_t i;
    for (i = 0; i < n; ++i)
        __asm__ __volatile__("" ::: "memory");
}

static uint32_t build_cmd(uint32_t opcode, uint8_t addr7, uint32_t m_param_bits)
{
    return (opcode << M_OPCODE_SHFT) |
           (((uint32_t)addr7 << SLV_ADDR_SHFT) & 0x0000FE00u) |
           m_param_bits;
}

static uint32_t i2c1_error_bits(void)
{
    return M_CMD_OVERRUN_EN |
           M_ILLEGAL_CMD_EN |
           M_CMD_FAILURE_EN |
           M_CMD_ABORT_EN;
}

static int i2c1_wait_idle(void)
{
    uint32_t spins = 0;
    while (spins++ < I2C1_SPIN_LIMIT)
    {
        if ((rd32(SE_GENI_STATUS) & GENI_M_CMD_ACTIVE) == 0u)
            return 0;
    }
    return -1;
}

static void i2c1_abort_if_needed(void)
{
    if (rd32(SE_GENI_STATUS) & GENI_M_CMD_ACTIVE)
    {
        wr32(SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
        io_barrier();
        tiny_delay(20000u);
        wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
        io_barrier();
    }
}

static int i2c1_wait_done(uint32_t *irq_out)
{
    uint32_t spins = 0;
    uint32_t last_irq = 0;

    for (;;)
    {
        uint32_t irq = rd32(SE_GENI_M_IRQ_STATUS);
        last_irq = irq;

        if (irq & M_CMD_DONE_EN)
        {
            if (irq_out)
                *irq_out = irq;
            return 0;
        }

        if (irq & i2c1_error_bits())
        {
            if (irq_out)
                *irq_out = irq;
            return -1;
        }

        if (++spins >= I2C1_SPIN_LIMIT)
        {
            if (irq_out)
                *irq_out = last_irq;
            return -2;
        }
    }
}

static int i2c1_wait_done_or_idle(uint32_t *irq_out)
{
    uint32_t spins = 0;
    uint32_t last_irq = 0;

    for (;;)
    {
        uint32_t irq = rd32(SE_GENI_M_IRQ_STATUS);
        uint32_t st  = rd32(SE_GENI_STATUS);

        last_irq = irq;

        if (irq & M_CMD_DONE_EN)
        {
            if (irq_out)
                *irq_out = irq;
            return 0;
        }

        if (irq & i2c1_error_bits())
        {
            if (irq_out)
                *irq_out = irq;
            return -1;
        }

        /*
          Important fallback:
          some paths appear to complete without a visible DONE irq bit.
          If the master sequencer is no longer active, treat that as completion.
        */
        if ((st & GENI_M_CMD_ACTIVE) == 0u)
        {
            if (irq_out)
                *irq_out = irq;
            return 0;
        }

        if (++spins >= I2C1_SPIN_LIMIT)
        {
            if (irq_out)
                *irq_out = last_irq;
            return -2;
        }
    }
}

static int i2c1_wait_read_ready(uint32_t wanted_len, uint32_t *irq_out)
{
    uint32_t spins = 0;
    uint32_t last_irq = 0;
    uint32_t wanted_words = (wanted_len + 3u) >> 2;

    for (;;)
    {
        uint32_t irq   = rd32(SE_GENI_M_IRQ_STATUS);
        uint32_t st    = rd32(SE_GENI_STATUS);
        uint32_t rx_st = rd32(SE_GENI_RX_FIFO_STATUS);
        uint32_t fifo_words = (rx_st & RX_FIFO_WC_MSK);

        last_irq = irq;

        if (irq & i2c1_error_bits())
        {
            if (irq_out)
                *irq_out = irq;
            return -1;
        }

        /*
          RX_FIFO_STATUS occupancy is FIFO words, not bytes.
        */
        if (fifo_words >= wanted_words)
        {
            if (irq_out)
                *irq_out = irq;
            return 0;
        }

        if (rx_st & RX_LAST)
        {
            if (irq_out)
                *irq_out = irq;
            return 0;
        }

        if ((irq & M_CMD_DONE_EN) && fifo_words != 0u)
        {
            if (irq_out)
                *irq_out = irq;
            return 0;
        }

        if (((st & GENI_M_CMD_ACTIVE) == 0u) && fifo_words != 0u)
        {
            if (irq_out)
                *irq_out = irq;
            return 0;
        }

        if (++spins >= I2C1_SPIN_LIMIT)
        {
            if (irq_out)
                *irq_out = last_irq;
            return -2;
        }
    }
}

/*
static int fifo_write_bytes(const uint8_t *buf, uint32_t len)
{
    uint32_t i;

    terminal_print("fifo wr len:");
    terminal_print_hex32(len);
    terminal_print(" txst0:");
    terminal_print_hex32(rd32(SE_GENI_TX_FIFO_STATUS));
    terminal_print("\n");

    for (i = 0; i < len; ++i)
    {
        wr32(SE_GENI_TX_FIFOn, (uint32_t)buf[i]);
        io_barrier();

        terminal_print("fifo b:");
        terminal_print_hex8(buf[i]);
        terminal_print(" txst:");
        terminal_print_hex32(rd32(SE_GENI_TX_FIFO_STATUS));
        terminal_print("\n");
    }

    terminal_print("fifo wr end txst:");
    terminal_print_hex32(rd32(SE_GENI_TX_FIFO_STATUS));
    terminal_print("\n");

    return 0;
}
*/

static int i2c1_wait_tx_watermark(uint32_t *irq_out)
{
    uint32_t spins = 0;

    for (;;)
    {
        uint32_t irq = rd32(SE_GENI_M_IRQ_STATUS);

        if (irq & M_TX_FIFO_WATERMARK_EN)
        {
            if (irq_out)
                *irq_out = irq;
            return 0;
        }

        if (irq & i2c1_error_bits())
        {
            if (irq_out)
                *irq_out = irq;
            return -1;
        }

        if (++spins >= I2C1_SPIN_LIMIT)
        {
            if (irq_out)
                *irq_out = irq;
            return -2;
        }
    }
}

static void fifo_write_bytes_now(const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i)
    {
        wr32(SE_GENI_TX_FIFOn, (uint32_t)buf[i]);
        io_barrier();
    }
}

static void i2c1_dump_rx_words_once(uint32_t expected_len)
{
    uint32_t rx_st = rd32(SE_GENI_RX_FIFO_STATUS);
    uint32_t bytes_avail = (rx_st & RX_FIFO_WC_MSK);
    uint32_t is_last = (rx_st & RX_LAST) ? 1u : 0u;
    uint32_t last_valid = ((rx_st & RX_LAST_BYTE_VALID_MSK) >> RX_LAST_BYTE_VALID_SHFT) + 1u;
    uint32_t words;
    uint32_t i;

    terminal_print("RXDBG st:");
    terminal_print_inline_hex32(rx_st);
    terminal_print_inline(" expected:");
    terminal_print_inline_hex32(expected_len);
    terminal_print_inline(" bytes:");
    terminal_print_inline_hex32(bytes_avail);
    terminal_print_inline(" last:");
    terminal_print_inline_hex32(is_last);
    terminal_print_inline(" last_valid:");
    terminal_print_inline_hex32(last_valid);
    terminal_print("\n");

    words = (bytes_avail + 3u) >> 2;

    for (i = 0; i < words; ++i)
    {
        uint32_t w = rd32(SE_GENI_RX_FIFOn);
        io_barrier();

        terminal_print("RXDBG w");
        terminal_print_hex32(i);
        terminal_print(":");
        terminal_print_hex32(w);
        terminal_print("\n");
    }
}

static int fifo_read_bytes(uint8_t *buf, uint32_t len)
{
    uint32_t got = 0;
    uint32_t spins = 0;

    if (!buf || len == 0u)
        return -1;

    while (got < len && spins++ < I2C1_SPIN_LIMIT)
    {
        uint32_t rx_st = rd32(SE_GENI_RX_FIFO_STATUS);
        uint32_t fifo_words = (rx_st & RX_FIFO_WC_MSK);
        uint32_t is_last = (rx_st & RX_LAST) ? 1u : 0u;
        uint32_t last_valid_field =
            (rx_st & RX_LAST_BYTE_VALID_MSK) >> RX_LAST_BYTE_VALID_SHFT;

        if (fifo_words == 0u)
            continue;

        /*
          GENI reports FIFO occupancy in FIFO words, not raw bytes.
          In your current packing setup we are using 8-bit packing with
          4 bytes per FIFO word.
        */
        while (fifo_words != 0u && got < len)
        {
            uint32_t word = rd32(SE_GENI_RX_FIFOn);
            uint32_t take = 4u;

            io_barrier();

            /*
              On the final FIFO word, RX_LAST_BYTE_VALID tells us how many
              bytes in that last word are valid. Linux treats values 1..3
              specially; otherwise a full 4 bytes are valid.
            */
            if (is_last && fifo_words == 1u)
            {
                if (last_valid_field != 0u && last_valid_field < 4u)
                    take = last_valid_field;
                else
                    take = 4u;
            }

            if (take > (len - got))
                take = (len - got);

            for (uint32_t i = 0; i < take; ++i)
                buf[got++] = (uint8_t)((word >> (i * 8u)) & 0xFFu);

            fifo_words--;
        }

        if (got >= len)
            return 0;

        if (is_last)
            break;
    }

    return (got == len) ? 0 : -1;
}

static void i2c1_drain_rx_junk(void)
{
    uint32_t spins = 0;

    while (spins++ < 256u)
    {
        uint32_t rx_st = rd32(SE_GENI_RX_FIFO_STATUS);
        uint32_t fifo_words = (rx_st & RX_FIFO_WC_MSK);

        if (fifo_words == 0u)
        {
            wr32(SE_GENI_M_IRQ_CLEAR, M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN);
            io_barrier();
            break;
        }

        while (fifo_words != 0u)
        {
            (void)rd32(SE_GENI_RX_FIFOn);
            io_barrier();
            fifo_words--;
        }

        wr32(SE_GENI_M_IRQ_CLEAR, M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN);
        io_barrier();

        if ((rd32(SE_GENI_RX_FIFO_STATUS) & RX_FIFO_WC_MSK) == 0u)
            break;
    }
}

int i2c1_bus_init(void)
{
    if (g_inited)
        return 0;

    /* Your logs show FIFO mode is available: IF_FIFO_DISABLE_RO = 0 */
    if (rd32(GENI_IF_FIFO_DISABLE_RO) & 1u)
    {
        terminal_error("i2c1: fifo disabled");
        return -1;
    }

    /* Same safe bring-up values that already produced DONE for 0x3A and 0x2C */
    wr32(GENI_OUTPUT_CTRL, 0x0000007Fu);
    wr32(GENI_CGC_CTRL, 0x0000007Fu);

    wr32(SE_GENI_DMA_MODE_EN, 0x00000000u);

    wr32(SE_GENI_CLK_SEL, 0x00000000u);
    wr32(GENI_SER_M_CLK_CFG, (7u << CLK_DIV_SHFT) | SER_CLK_EN);
    wr32(SE_I2C_SCL_COUNTERS, (10u << 20) | (11u << 10) | 26u);

    /* FIFO mode */
    wr32(SE_GENI_BYTE_GRAN, 1u);
    wr32(SE_GENI_TX_WATERMARK_REG, 0u);
    wr32(SE_GENI_RX_WATERMARK_REG, 0u);

    /* 8-bit packing */
    wr32(SE_GENI_TX_PACKING_CFG0, 0x0000001Fu);
    wr32(SE_GENI_TX_PACKING_CFG1, 0u);
    wr32(SE_GENI_RX_PACKING_CFG0, 0x0000001Fu);
    wr32(SE_GENI_RX_PACKING_CFG1, 0u);

    terminal_print("i2c1 cfg ok\n");

    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();
    tiny_delay(20000u);

    i2c1_abort_if_needed();

    if (i2c1_wait_idle() != 0)
    {
        terminal_error("i2c1: not idle");
        return -1;
    }

    g_inited = 1;
    terminal_success("i2c1 bus init ok");
    return 0;
}

int i2c1_bus_write(uint8_t addr7, const void *tx, uint32_t tx_len)
{
    const uint8_t *src = (const uint8_t *)tx;
    uint32_t irq = 0;
    int rc;

    if (!g_inited && i2c1_bus_init() != 0)
        return -1;

    if (!src || tx_len == 0)
        return -1;

    if (i2c1_wait_idle() != 0)
        return -1;

    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();
    i2c1_drain_rx_junk();

    wr32(SE_I2C_TX_TRANS_LEN, tx_len);
    wr32(SE_I2C_RX_TRANS_LEN, 0u);

    /* ask controller to request TX data */
    wr32(SE_GENI_TX_WATERMARK_REG, 1u);
    io_barrier();

    /* launch command first */
    wr32(SE_GENI_M_CMD0, build_cmd(I2C_WRITE, addr7, 0u));
    io_barrier();

    rc = i2c1_wait_tx_watermark(&irq);

    if (!g_i2c1_quiet)
    {
        terminal_print("i2c wr wm a:");
        terminal_print_inline_hex8(addr7);
        terminal_print_inline(" rc:");
        terminal_print_inline_hex32((uint32_t)rc);
        terminal_print_inline(" irq:");
        terminal_print_inline_hex32(irq);
        terminal_print_inline(" txst:");
        terminal_print_inline_hex32(rd32(SE_GENI_TX_FIFO_STATUS));
        terminal_print("\n");
    }
    if (rc != 0)
    {
        wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
        io_barrier();
        i2c1_abort_if_needed();
        return -1;
    }

    /* clear watermark event and feed FIFO */
    wr32(SE_GENI_M_IRQ_CLEAR, M_TX_FIFO_WATERMARK_EN);
    io_barrier();

    fifo_write_bytes_now(src, tx_len);

    /* all TX data queued: stop watermark requests now */
    wr32(SE_GENI_TX_WATERMARK_REG, 0u);
    wr32(SE_GENI_M_IRQ_CLEAR, M_TX_FIFO_WATERMARK_EN);
    io_barrier();

    rc = i2c1_wait_done_or_idle(&irq);

    if (!g_i2c1_quiet)
    {
        terminal_print("i2c wr phase a:");
        terminal_print_inline_hex8(addr7);
        terminal_print_inline(" rc:");
        terminal_print_inline_hex32((uint32_t)rc);
        terminal_print_inline(" irq:");
        terminal_print_inline_hex32(irq);
        terminal_print_inline(" st:");
        terminal_print_inline_hex32(rd32(SE_GENI_STATUS));
        terminal_print_inline(" rxst:");
        terminal_print_inline_hex32(rd32(SE_GENI_RX_FIFO_STATUS));

        if (rd32(SE_GENI_RX_FIFO_STATUS) != 0u)
            i2c1_drain_rx_junk();

        terminal_print_inline(" txst:");
        terminal_print_inline_hex32(rd32(SE_GENI_TX_FIFO_STATUS));
        terminal_print("\n");
    }

    wr32(SE_GENI_TX_WATERMARK_REG, 0u);
    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();

    if (rc != 0)
    {
        i2c1_abort_if_needed();
        return -1;
    }

    return 0;
}

int i2c1_bus_write_read(uint8_t addr7,
                        const void *tx, uint32_t tx_len,
                        void *rx, uint32_t rx_len)
{
    const uint8_t *src = (const uint8_t *)tx;
    uint8_t *dst = (uint8_t *)rx;
    uint32_t irq = 0;
    int rc;

    if (!g_inited && i2c1_bus_init() != 0)
        return -1;

    if ((!src && tx_len) || (!dst && rx_len))
        return -1;

    if (tx_len == 0 || rx_len == 0)
        return -1;

    if (i2c1_wait_idle() != 0)
        return -1;

    /*
      Phase 1:
      write the register pointer, but hold the bus for a repeated start.
    */
    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();
    i2c1_drain_rx_junk();

    wr32(SE_I2C_TX_TRANS_LEN, tx_len);
    wr32(SE_I2C_RX_TRANS_LEN, 0u);

    wr32(SE_GENI_TX_WATERMARK_REG, 1u);
    io_barrier();

    wr32(SE_GENI_M_CMD0, build_cmd(I2C_WRITE, addr7, STOP_STRETCH));
    io_barrier();

    rc = i2c1_wait_tx_watermark(&irq);
    if (rc != 0)
    {
        wr32(SE_GENI_TX_WATERMARK_REG, 0u);
        wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
        io_barrier();
        i2c1_abort_if_needed();
        return -1;
    }

    wr32(SE_GENI_M_IRQ_CLEAR, M_TX_FIFO_WATERMARK_EN);
    io_barrier();

    fifo_write_bytes_now(src, tx_len);

    wr32(SE_GENI_TX_WATERMARK_REG, 0u);
    wr32(SE_GENI_M_IRQ_CLEAR, M_TX_FIFO_WATERMARK_EN);
    io_barrier();

    rc = i2c1_wait_done_or_idle(&irq);
    if (rc != 0)
    {
        wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
        io_barrier();
        i2c1_abort_if_needed();
        return -1;
    }

    /*
      Phase 2:
      repeated-start into read.
    */
    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();

    wr32(SE_I2C_TX_TRANS_LEN, 0u);
    wr32(SE_I2C_RX_TRANS_LEN, rx_len);

    wr32(SE_GENI_M_CMD0, build_cmd(I2C_READ, addr7, 0u));
    io_barrier();

    rc = i2c1_wait_read_ready(rx_len, &irq);

    if (!g_i2c1_quiet)
    {
        terminal_print("i2c reg-rd a:");
        terminal_print_inline_hex8(addr7);
        terminal_print_inline(" rc:");
        terminal_print_inline_hex32((uint32_t)rc);
        terminal_print_inline(" irq:");
        terminal_print_inline_hex32(irq);
        terminal_print_inline(" rxst:");
        terminal_print_inline_hex32(rd32(SE_GENI_RX_FIFO_STATUS));
        terminal_print("\n");
    }

    if (rc != 0)
    {
        wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
        io_barrier();
        i2c1_abort_if_needed();
        return -1;
    }

    if (fifo_read_bytes(dst, rx_len) != 0)
    {
        terminal_print("i2c reg-rd drain fail a:");
        terminal_print_hex8(addr7);
        terminal_print(" rxst:");
        terminal_print_hex32(rd32(SE_GENI_RX_FIFO_STATUS));
        terminal_print(" irq:");
        terminal_print_hex32(rd32(SE_GENI_M_IRQ_STATUS));
        terminal_print("\n");

        wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
        io_barrier();
        i2c1_abort_if_needed();
        return -1;
    }

    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();

    return 0;
}

int i2c1_bus_read(uint8_t addr7, void *rx, uint32_t rx_len)
{
    uint8_t *dst = (uint8_t *)rx;
    uint32_t irq = 0;
    int rc;

    if (!g_inited && i2c1_bus_init() != 0)
        return -1;

    if (!dst || rx_len == 0)
        return -1;

    if (i2c1_wait_idle() != 0)
        return -1;

    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();
    i2c1_drain_rx_junk();

    wr32(SE_I2C_TX_TRANS_LEN, 0u);
    wr32(SE_I2C_RX_TRANS_LEN, rx_len);

    wr32(SE_GENI_M_CMD0, build_cmd(I2C_READ, addr7, 0u));
    io_barrier();

    rc = i2c1_wait_read_ready(rx_len, &irq);

    if (!g_i2c1_quiet)
    {
        terminal_print("i2c rd a:");
        terminal_print_inline_hex8(addr7);
        terminal_print_inline(" rc:");
        terminal_print_inline_hex32((uint32_t)rc);
        terminal_print_inline(" irq:");
        terminal_print_inline_hex32(irq);
        terminal_print_inline(" rxst:");
        terminal_print_inline_hex32(rd32(SE_GENI_RX_FIFO_STATUS));
        terminal_print("\n");
    }

    if (rc != 0)
    {
        wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
        io_barrier();
        i2c1_abort_if_needed();
        return -1;
    }

    if (fifo_read_bytes(dst, rx_len) != 0)
    {
        terminal_print("i2c rd drain fail a:");
        terminal_print_inline_hex8(addr7);
        terminal_print_inline(" rxst:");
        terminal_print_inline_hex32(rd32(SE_GENI_RX_FIFO_STATUS));
        terminal_print_inline(" irq:");
        terminal_print_inline_hex32(rd32(SE_GENI_M_IRQ_STATUS));
        terminal_print("\n");

        wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
        io_barrier();
        i2c1_abort_if_needed();
        return -1;
    }

    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();

    return 0;
}

int i2c1_bus_addr_only(uint8_t addr7)
{
    uint32_t irq = 0;
    int rc;

    if (!g_inited && i2c1_bus_init() != 0)
        return -1;

    if (i2c1_wait_idle() != 0)
        return -1;

    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();
    i2c1_drain_rx_junk();

    wr32(SE_I2C_TX_TRANS_LEN, 0u);
    wr32(SE_I2C_RX_TRANS_LEN, 0u);

    wr32(SE_GENI_M_CMD0, build_cmd(I2C_ADDR_ONLY, addr7, 0u));
    io_barrier();

    rc = i2c1_wait_done(&irq);

    terminal_print("i2c addr a:");
    terminal_print_inline_hex8(addr7);
    terminal_print_inline(" rc:");
    terminal_print_inline_hex32((uint32_t)rc);
    terminal_print_inline(" irq:");
    terminal_print_inline_hex32(irq);
    terminal_print("\n");

    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();

    if (rc != 0)
    {
        i2c1_abort_if_needed();
        return -1;
    }

    return 0;
}

int i2c1_bus_write_read_combined(uint8_t addr7,
                                 const void *tx, uint32_t tx_len,
                                 void *rx, uint32_t rx_len)
{
    const uint8_t *src = (const uint8_t *)tx;
    uint8_t *dst = (uint8_t *)rx;
    uint32_t irq = 0;
    int rc;

    if (!g_inited && i2c1_bus_init() != 0)
        return -1;

    if ((!src && tx_len) || (!dst && rx_len))
        return -1;

    if (tx_len == 0 || rx_len == 0)
        return -1;

    if (i2c1_wait_idle() != 0)
        return -1;

    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();
    i2c1_drain_rx_junk();

    wr32(SE_I2C_TX_TRANS_LEN, tx_len);
    wr32(SE_I2C_RX_TRANS_LEN, rx_len);

    wr32(SE_GENI_TX_WATERMARK_REG, 1u);
    io_barrier();

    /* single combined controller transaction */
    wr32(SE_GENI_M_CMD0, build_cmd(I2C_WRITE_READ, addr7, 0u));
    io_barrier();

    rc = i2c1_wait_tx_watermark(&irq);
    if (rc != 0)
    {
        wr32(SE_GENI_TX_WATERMARK_REG, 0u);
        wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
        io_barrier();
        i2c1_abort_if_needed();
        return -1;
    }

    wr32(SE_GENI_M_IRQ_CLEAR, M_TX_FIFO_WATERMARK_EN);
    io_barrier();

    fifo_write_bytes_now(src, tx_len);

    wr32(SE_GENI_TX_WATERMARK_REG, 0u);
    wr32(SE_GENI_M_IRQ_CLEAR, M_TX_FIFO_WATERMARK_EN);
    io_barrier();

    rc = i2c1_wait_done(&irq);

    if (!g_i2c1_quiet)
    {
        terminal_print("i2c comb a:");
        terminal_print_hex8(addr7);
        terminal_print(" rc:");
        terminal_print_hex32((uint32_t)rc);
        terminal_print(" irq:");
        terminal_print_hex32(irq);
        terminal_print(" rxst:");
        terminal_print_hex32(rd32(SE_GENI_RX_FIFO_STATUS));
        terminal_print("\n");
    }

    if (rc != 0)
    {
        wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
        io_barrier();
        i2c1_abort_if_needed();
        return -1;
    }

    if (fifo_read_bytes(dst, rx_len) != 0)
    {
        terminal_print("i2c comb drain fail a:");
        terminal_print_hex8(addr7);
        terminal_print(" rxst:");
        terminal_print_hex32(rd32(SE_GENI_RX_FIFO_STATUS));
        terminal_print(" irq:");
        terminal_print_hex32(rd32(SE_GENI_M_IRQ_STATUS));
        terminal_print("\n");

        wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
        io_barrier();
        i2c1_abort_if_needed();
        return -1;
    }

    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();

    return 0;
}