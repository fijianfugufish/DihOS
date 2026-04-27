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

/*
  Matches Linux geni_se_config_packing(se, 8, 4, true, ...):
  8-bit protocol words, four words packed per FIFO entry, MSB-to-LSB vectors.
*/
#define I2C1_PACK_8BIT_CFG0 0x0007F8FEu
#define I2C1_PACK_8BIT_CFG1 0x000FFEFEu

static uint8_t g_inited = 0;

static uint8_t g_i2c1_quiet = 0;
static uint32_t g_i2c1_zero_dump_budget = 8u;

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

static uint32_t i2c1_rx_word_count(uint32_t rx_st)
{
    return (rx_st & RX_FIFO_WC_MSK);
}

static uint8_t i2c1_buffer_all_zero(const uint8_t *buf, uint32_t len)
{
    if (!buf)
        return 1u;

    for (uint32_t i = 0; i < len; ++i)
    {
        if (buf[i] != 0u)
            return 0u;
    }

    return 1u;
}

static void i2c1_log_zero_payload_words(uint32_t first_rx_st,
                                        const uint32_t *words,
                                        uint32_t word_count,
                                        uint32_t want_len)
{
    if (g_i2c1_quiet)
        return;

    if (g_i2c1_zero_dump_budget == 0u)
        return;

    /* Keep the forensic dump available in code, but disabled by default. */
    return;

    g_i2c1_zero_dump_budget--;

    terminal_print("i2c rx zero payload st:");
    terminal_print_hex32(first_rx_st);
    terminal_print(" want:");
    terminal_print_hex32(want_len);
    terminal_print(" words:");
    terminal_print_hex32(word_count);
    terminal_print("\n");

    for (uint32_t i = 0; i < word_count; ++i)
    {
        terminal_print("i2c rx w");
        terminal_print_hex32(i);
        terminal_print(":");
        terminal_print_hex32(words[i]);
        terminal_print("\n");
    }
}

static void i2c1_drain_rx_junk(void);

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
        uint32_t fifo_words = i2c1_rx_word_count(rx_st);

        last_irq = irq;

        if (irq & i2c1_error_bits())
        {
            if (irq_out)
                *irq_out = irq;
            return -1;
        }

        /*
          With the proper 4x8 packing enabled, the low field tracks FIFO
          words. A 30-byte HID descriptor shows up as 8 FIFO words plus a
          short final-word indication in RX_LAST_BYTE_VALID.
        */
        if (fifo_words >= wanted_words)
        {
            if (irq_out)
                *irq_out = irq;
            return 0;
        }

        /*
          Large HID descriptor reads can be longer than the RX FIFO can hold
          at once. If we wait for the whole transfer to appear before starting
          to drain, the controller can stall right around the FIFO ceiling.
          Start streaming as soon as any FIFO words are present for those
          longer reads and let fifo_read_bytes() keep draining as more data
          arrives.
        */
        if (wanted_words > 24u && fifo_words != 0u)
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
    uint32_t i = 0;

    while (i < len)
    {
        uint32_t word = 0u;
        uint32_t take = 0u;

        while (take < 4u && i < len)
        {
            word |= ((uint32_t)buf[i]) << (take * 8u);
            take++;
            i++;
        }

        wr32(SE_GENI_TX_FIFOn, word);
        io_barrier();
    }
}

static int i2c1_run_write_phase(uint8_t addr7,
                                const uint8_t *src,
                                uint32_t tx_len,
                                uint32_t m_param_bits,
                                uint32_t *wm_irq_out,
                                uint32_t *phase_irq_out)
{
    uint32_t wm_irq = 0;
    uint32_t phase_irq = 0;
    int rc;

    if (i2c1_wait_idle() != 0)
        return -1;

    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();
    i2c1_drain_rx_junk();

    wr32(SE_I2C_TX_TRANS_LEN, tx_len);
    wr32(SE_I2C_RX_TRANS_LEN, 0u);

    wr32(SE_GENI_TX_WATERMARK_REG, 1u);
    io_barrier();

    wr32(SE_GENI_M_CMD0, build_cmd(I2C_WRITE, addr7, m_param_bits));
    io_barrier();

    rc = i2c1_wait_tx_watermark(&wm_irq);
    if (rc == 0)
    {
        wr32(SE_GENI_M_IRQ_CLEAR, M_TX_FIFO_WATERMARK_EN);
        io_barrier();

        fifo_write_bytes_now(src, tx_len);

        wr32(SE_GENI_TX_WATERMARK_REG, 0u);
        wr32(SE_GENI_M_IRQ_CLEAR, M_TX_FIFO_WATERMARK_EN);
        io_barrier();

        rc = i2c1_wait_done_or_idle(&phase_irq);
    }

    wr32(SE_GENI_TX_WATERMARK_REG, 0u);
    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();

    if (wm_irq_out)
        *wm_irq_out = wm_irq;
    if (phase_irq_out)
        *phase_irq_out = phase_irq;

    if (rc != 0)
    {
        i2c1_abort_if_needed();
        return -1;
    }

    return 0;
}

static void i2c1_dump_rx_words_once(uint32_t expected_len)
{
    uint32_t rx_st = rd32(SE_GENI_RX_FIFO_STATUS);
    uint32_t fifo_words = i2c1_rx_word_count(rx_st);
    uint32_t is_last = (rx_st & RX_LAST) ? 1u : 0u;
    uint32_t last_valid = ((rx_st & RX_LAST_BYTE_VALID_MSK) >> RX_LAST_BYTE_VALID_SHFT);
    uint32_t i;

    terminal_print("RXDBG st:");
    terminal_print_inline_hex32(rx_st);
    terminal_print_inline(" expected:");
    terminal_print_inline_hex32(expected_len);
    terminal_print_inline(" words:");
    terminal_print_inline_hex32(fifo_words);
    terminal_print_inline(" last:");
    terminal_print_inline_hex32(is_last);
    terminal_print_inline(" last_valid:");
    terminal_print_inline_hex32(last_valid);
    terminal_print("\n");

    for (i = 0; i < fifo_words; ++i)
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
    uint32_t first_rx_st = 0u;
    uint32_t sample_words[8];
    uint32_t sample_word_count = 0u;

    if (!buf || len == 0u)
        return -1;

    while (got < len && spins++ < I2C1_SPIN_LIMIT)
    {
        uint32_t rx_st = rd32(SE_GENI_RX_FIFO_STATUS);
        uint32_t fifo_words = i2c1_rx_word_count(rx_st);
        uint32_t is_last = (rx_st & RX_LAST) ? 1u : 0u;
        uint32_t last_valid_field =
            (rx_st & RX_LAST_BYTE_VALID_MSK) >> RX_LAST_BYTE_VALID_SHFT;

        if (fifo_words == 0u)
            continue;

        if (first_rx_st == 0u)
            first_rx_st = rx_st;

        /*
          RX_FIFO_STATUS reports FIFO words. Each FIFO pop yields up to four
          packed bytes, and the final word may be short.
        */
        while (fifo_words != 0u && got < len)
        {
            uint32_t word = rd32(SE_GENI_RX_FIFOn);
            uint32_t take = 4u;

            io_barrier();

            if (sample_word_count < (sizeof(sample_words) / sizeof(sample_words[0])))
                sample_words[sample_word_count++] = word;

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
            break;

        if (is_last)
            break;
    }

    if (got == len)
    {
        if (i2c1_buffer_all_zero(buf, len))
            i2c1_log_zero_payload_words(first_rx_st,
                                        sample_words,
                                        sample_word_count,
                                        len);
        return 0;
    }

    return -1;
}

static void i2c1_drain_rx_junk(void)
{
    uint32_t spins = 0;

    while (spins++ < 256u)
    {
        uint32_t rx_st = rd32(SE_GENI_RX_FIFO_STATUS);
        uint32_t fifo_words = i2c1_rx_word_count(rx_st);

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

        if (i2c1_rx_word_count(rd32(SE_GENI_RX_FIFO_STATUS)) == 0u)
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
    /*
      Qualcomm GENI BYTE_GRAN meanings:
        0 = 4x8
        1 = 2x16
        2 = 1x32

      We are doing 8-bit packed FIFO transfers here, so this must be 0,
      not 1.
    */
    wr32(SE_GENI_BYTE_GRAN, 0u);
    wr32(SE_GENI_TX_WATERMARK_REG, 0u);
    wr32(SE_GENI_RX_WATERMARK_REG, 0u);

    /* 8-bit 4x8 packing, same vector layout used by Linux on GENI I2C */
    wr32(SE_GENI_TX_PACKING_CFG0, I2C1_PACK_8BIT_CFG0);
    wr32(SE_GENI_TX_PACKING_CFG1, I2C1_PACK_8BIT_CFG1);
    wr32(SE_GENI_RX_PACKING_CFG0, I2C1_PACK_8BIT_CFG0);
    wr32(SE_GENI_RX_PACKING_CFG1, I2C1_PACK_8BIT_CFG1);

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
    uint32_t wm_irq = 0;
    uint32_t phase_irq = 0;
    int rc;

    if (!g_inited && i2c1_bus_init() != 0)
        return -1;

    if (!src || tx_len == 0)
        return -1;

    rc = i2c1_run_write_phase(addr7,
                              src,
                              tx_len,
                              0u,
                              &wm_irq,
                              &phase_irq);

    if (!g_i2c1_quiet)
    {
        terminal_print("i2c wr wm a:");
        terminal_print_inline_hex8(addr7);
        terminal_print_inline(" rc:");
        terminal_print_inline_hex32((uint32_t)((rc == 0) ? 0 : 0xFFFFFFFFu));
        terminal_print_inline(" irq:");
        terminal_print_inline_hex32(wm_irq);
        terminal_print_inline(" txst:");
        terminal_print_inline_hex32(rd32(SE_GENI_TX_FIFO_STATUS));
        terminal_print("\n");

        terminal_print("i2c wr phase a:");
        terminal_print_inline_hex8(addr7);
        terminal_print_inline(" rc:");
        terminal_print_inline_hex32((uint32_t)((rc == 0) ? 0 : 0xFFFFFFFFu));
        terminal_print_inline(" irq:");
        terminal_print_inline_hex32(phase_irq);
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

    if (rc != 0)
        return -1;

    return 0;
}

int i2c1_bus_write_read(uint8_t addr7,
                        const void *tx, uint32_t tx_len,
                        void *rx, uint32_t rx_len)
{
    const uint8_t *src = (const uint8_t *)tx;
    uint8_t *dst = (uint8_t *)rx;
    uint32_t wm_irq = 0;
    uint32_t phase_irq = 0;
    uint32_t irq = 0;
    int rc;

    if (!g_inited && i2c1_bus_init() != 0)
        return -1;

    if ((!src && tx_len) || (!dst && rx_len))
        return -1;

    if (tx_len == 0 || rx_len == 0)
        return -1;

    rc = i2c1_run_write_phase(addr7,
                              src,
                              tx_len,
                              STOP_STRETCH,
                              &wm_irq,
                              &phase_irq);
    if (rc != 0)
        return -1;

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

    rc = fifo_read_bytes(dst, rx_len);

    wr32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    io_barrier();

    if (rc != 0)
    {
        i2c1_abort_if_needed();
        return -1;
    }

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

    rc = i2c1_wait_read_ready(rx_len, &irq);

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
