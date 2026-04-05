#include "i2c/i2c1_hidi2c.h"
#include "terminal/terminal_api.h"
#include "hardware_probes/acpi_probe_hidi2c_ready.h"
#include "acpi/aml_tiny.h"
#include "gpio/gpio.h"
#include "kwrappers/string.h"
#include <stdint.h>

#define TCPD_ADDR_ACPI 0x2Cu
#define TCPD_DESC_REG_ACPI 0x0020u
#define HIDI2C_DESC_REG_FALLBACK 0x0000u

#define I2C_HID_OPCODE_RESET 0x01u
#define I2C_HID_OPCODE_SET_POWER 0x08u
#define I2C_HID_PWR_ON 0x00u

static hidi2c_device g_kbd;
static hidi2c_device g_tpd;
static uint8_t g_bus_ready = 0;

static uint8_t g_hidi2c_debug = 0;

extern int i2c1_bus_init(void);
extern int i2c1_bus_write(uint8_t addr7, const void *tx, uint32_t tx_len);
extern int i2c1_bus_read(uint8_t addr7, void *rx, uint32_t rx_len);
extern int i2c1_bus_write_read(uint8_t addr7, const void *tx, uint32_t tx_len, void *rx, uint32_t rx_len);
extern int i2c1_bus_write_read_combined(uint8_t addr7, const void *tx, uint32_t tx_len, void *rx, uint32_t rx_len);
extern void i2c1_bus_set_quiet(uint8_t quiet);
extern int i2c1_bus_addr_only(uint8_t addr7);

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void wr16be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void short_delay(uint32_t n)
{
    volatile uint32_t i;
    for (i = 0; i < n; ++i)
        __asm__ __volatile__("" ::: "memory");
}

static void delay_us_approx(uint32_t us)
{
    /* crude busy-wait placeholder; tune later if needed */
    short_delay(us * 200u);
}

static void delay_ms_approx(uint32_t ms)
{
    while (ms--)
        delay_us_approx(1000u);
}

static int tcpd_probe_address_linuxish(hidi2c_device *dev)
{
    uint8_t b = 0;
    int rc;

    if (!dev)
        return -1;

    /*
      Linux does a raw byte probe and retries after 400-500us because some
      devices only wake after the first clock edge.
    */
    rc = i2c1_bus_read(dev->i2c_addr_7bit, &b, 1u);
    if (rc != 0)
    {
        delay_us_approx(500u);
        rc = i2c1_bus_read(dev->i2c_addr_7bit, &b, 1u);
    }

    terminal_print("TCPD probe-byte rc:");
    terminal_print_hex32((uint32_t)rc);
    terminal_print(" b:");
    terminal_print_hex8(b);
    terminal_print("\n");

    return rc;
}

static uint64_t g_aml_lids = 0;
static uint64_t g_aml_lidr = 0;
static uint64_t g_aml_gabl = 0;
static uint64_t g_aml_lidb = 0;

static void norm_acpi_name(const char *src, char *dst, uint32_t cap)
{
    uint32_t w = 0;
    uint32_t i = 0;

    if (!dst || cap == 0)
        return;

    dst[0] = 0;

    if (!src)
        return;

    while (src[i] && w + 1u < cap)
    {
        char c = src[i++];

        /* accept either dots or no dots */
        if (c == '.')
            continue;

        /* normalize root separators */
        if (c == '/')
            c = '\\';

        if (c >= 'a' && c <= 'z')
            c -= 32;

        dst[w++] = c;
    }

    dst[w] = 0;
}

static int normeq(const char *a, const char *b)
{
    char na[96];
    char nb[96];
    uint32_t i = 0;

    norm_acpi_name(a, na, sizeof(na));
    norm_acpi_name(b, nb, sizeof(nb));

    while (na[i] && nb[i])
    {
        if (na[i] != nb[i])
            return 0;
        ++i;
    }

    return (na[i] == 0 && nb[i] == 0);
}

static int acpi_name_has(const char *path, const char *needle)
{
    char np[96];
    char nn[64];

    norm_acpi_name(path, np, sizeof(np));
    norm_acpi_name(needle, nn, sizeof(nn));

    return strcontains(np, nn);
}

static int tcpd_aml_read_named_int(void *user, const char *name, uint64_t *out)
{
    (void)user;

    if (!out)
        return -1;

    /* known integer objects */

    if (name && acpi_name_has(name, "GIO0.GABL"))
    {
        *out = g_aml_gabl;
        return 0;
    }

    if (name && acpi_name_has(name, "LID0.LIDS"))
    {
        *out = g_aml_lids;
        return 0;
    }

    if (name && acpi_name_has(name, "LID0.LIDB"))
    {
        *out = g_aml_lidb;
        return 0;
    }

    if (name && acpi_name_has(name, "GIO0.LIDR"))
    {
        *out = g_aml_lidr;
        return 0;
    }

    /*
     IMPORTANT:
     Do NOT fail namespace lookup.
     Unknown ACPI objects should default to zero.
    */

    *out = 0;

    terminal_print("AML namespace auto-zero: ");
    if (name)
        terminal_print(name);
    terminal_print("\n");

    return 0;
}

static int tcpd_aml_write_named_int(void *user, const char *name, uint64_t value)
{
    (void)user;

    if (!name)
        return 0;

    if (acpi_name_has(name, "GIO0.GABL"))
    {
        g_aml_gabl = value;

        terminal_print("AML write GABL=");
        terminal_print_hex32((uint32_t)value);
        terminal_print("\n");

        return 0;
    }

    if (acpi_name_has(name, "LID0.LIDB"))
    {
        g_aml_lidb = value;

        terminal_print("AML write LIDB=");
        terminal_print_hex32((uint32_t)value);
        terminal_print("\n");

        return 0;
    }

    if (acpi_name_has(name, "GIO0.LIDR"))
    {
        g_aml_lidr = value;

        terminal_print("AML write LIDR=");
        terminal_print_hex32((uint32_t)value);
        terminal_print("\n");

        return 0;
    }

    terminal_print("AML ignored write: ");
    terminal_print(name);
    terminal_print("\n");

    return 0;
}

static void tcpd_aml_log(void *user, const char *msg)
{
    (void)user;
    terminal_print("AML: ");
    terminal_print(msg);
    terminal_print("\n");
}

static void tcpd_try_method_from_acpi_ex(const char *tag,
                                         const char *scope_prefix,
                                         const uint8_t *body,
                                         uint16_t len,
                                         uint8_t valid,
                                         const uint64_t *args,
                                         uint32_t arg_count)
{
    aml_tiny_method m;
    aml_tiny_host h;
    uint64_t ret = 0;
    int rc;

    if (!valid || !body || len == 0u)
    {
        terminal_print("AML: no ");
        terminal_print(tag);
        terminal_print(" body available\n");
        return;
    }

    m.aml = body;
    m.aml_len = len;
    m.scope_prefix = scope_prefix ? scope_prefix : "\\_SB.D0?_";
    m.arg_count = arg_count;

    for (uint32_t i = 0; i < 7u; ++i)
        m.args[i] = 0;

    for (uint32_t i = 0; i < arg_count && i < 7u; ++i)
        m.args[i] = args[i];

    h.read_named_int = tcpd_aml_read_named_int;
    h.write_named_int = tcpd_aml_write_named_int;
    h.log = tcpd_aml_log;
    h.user = 0;

    terminal_print("AML: exec ");
    terminal_print(tag);
    terminal_print(" len:");
    terminal_print_hex32(len);
    terminal_print("\n");

    rc = aml_tiny_trace_names(&m, &h);
    terminal_print("AML trace rc:");
    terminal_print_hex32((uint32_t)rc);
    terminal_print("\n");

    rc = aml_tiny_exec(&m, &h, &ret);
    terminal_print("AML exec rc:");
    terminal_print_hex32((uint32_t)rc);
    terminal_print(" ret:");
    terminal_print_hex32((uint32_t)ret);
    terminal_print("\n");
}

static void tcpd_try_method_from_acpi(const char *tag,
                                      const char *scope_prefix,
                                      const uint8_t *body,
                                      uint16_t len,
                                      uint8_t valid)
{
    tcpd_try_method_from_acpi_ex(tag, scope_prefix, body, len, valid, 0, 0);
}

static uint8_t aml_body_present(const uint8_t *body, uint16_t len, uint8_t valid)
{
    if (valid)
        return 1;
    if (body && len != 0u)
        return 1;
    return 0;
}

static void tcpd_try_ps3_from_acpi(const hidi2c_acpi_regs *regs)
{
    if (!regs)
        return;
    tcpd_try_method_from_acpi("_PS3",
                              "\\_SB.D0?_",
                              regs->tcpd_ps3_body,
                              regs->tcpd_ps3_len,
                              aml_body_present(regs->tcpd_ps3_body,
                                               regs->tcpd_ps3_len,
                                               regs->tcpd_ps3_valid));
}

static void tcpd_try_sta_from_acpi(const hidi2c_acpi_regs *regs)
{
    if (!regs)
        return;
    tcpd_try_method_from_acpi("_STA",
                              "\\_SB.D0?_",
                              regs->tcpd_sta_body,
                              regs->tcpd_sta_len,
                              aml_body_present(regs->tcpd_sta_body,
                                               regs->tcpd_sta_len,
                                               regs->tcpd_sta_valid));
}

static void tcpd_try_ini_from_acpi(const hidi2c_acpi_regs *regs)
{
    if (!regs)
        return;
    tcpd_try_method_from_acpi("_INI",
                              "\\_SB.D0?_",
                              regs->tcpd_ini_body,
                              regs->tcpd_ini_len,
                              aml_body_present(regs->tcpd_ini_body,
                                               regs->tcpd_ini_len,
                                               regs->tcpd_ini_valid));
}

static void tcpd_try_ps0_from_acpi(const hidi2c_acpi_regs *regs)
{
    if (!regs)
        return;
    tcpd_try_method_from_acpi("_PS0",
                              "\\_SB.D0?_",
                              regs->tcpd_ps0_body,
                              regs->tcpd_ps0_len,
                              aml_body_present(regs->tcpd_ps0_body,
                                               regs->tcpd_ps0_len,
                                               regs->tcpd_ps0_valid));
}

static void tcpd_try_gio0_reg_from_acpi(const hidi2c_acpi_regs *regs)
{
    uint64_t args[2];

    if (!regs)
        return;

    /*
      GIO0._REG checks Arg0 against 0x08, then stores Arg1 into GABL.
      Use _REG(0x08, 1) to simulate OpRegion connect/enable.
    */
    args[0] = 0x08u;
    args[1] = 1u;

    tcpd_try_method_from_acpi_ex("GIO0._REG",
                                 "\\_SB.GIO0",
                                 regs->tcpd_gio0_reg_body,
                                 regs->tcpd_gio0_reg_len,
                                 regs->tcpd_gio0_reg_valid,
                                 args,
                                 2u);
}

static void tcpd_try_tcpd_dsm_from_acpi(const hidi2c_acpi_regs *regs)
{
    if (!regs)
        return;

    /*
      Placeholder runner only.
      Do not call this until the probe also exports the correct _DSM UUID/rev/fn
      inputs, because _DSM is argument-driven.
    */
    tcpd_try_method_from_acpi("_DSM",
                              "\\_SB.D0?_",
                              regs->tcpd_dsm_body,
                              regs->tcpd_dsm_len,
                              regs->tcpd_dsm_valid);
}

static void tcpd_try_gio0_dsm_from_acpi(const hidi2c_acpi_regs *regs)
{
    if (!regs)
        return;

    /*
      Placeholder runner only.
      Do not call this until the probe also exports the correct _DSM UUID/rev/fn
      inputs, because _DSM is argument-driven.
    */
    tcpd_try_method_from_acpi("_DSM",
                              "\\_SB.GIO0",
                              regs->tcpd_gio0_dsm_body,
                              regs->tcpd_gio0_dsm_len,
                              regs->tcpd_gio0_dsm_valid);
}

static void hidi2c_touchpad_wake_probe(hidi2c_device *dev)
{
    uint8_t reg0[2];

    if (!dev)
        return;

    terminal_print("TCPD: minimal wake probe\n");

    i2c1_bus_set_quiet(1);

    /* 1) address-only nudge */
    (void)i2c1_bus_addr_only(dev->i2c_addr_7bit);
    delay_us_approx(1000u);

    /* 2) write descriptor register pointer */
    wr16(reg0, TCPD_DESC_REG_ACPI);
    (void)i2c1_bus_write(dev->i2c_addr_7bit, reg0, 2);
    delay_ms_approx(20u);

    i2c1_bus_set_quiet(0);
}

static void tcpd_gpio_reset_pulse(const hidi2c_acpi_regs *regs)
{
    uint32_t active;
    uint32_t inactive;

    if (!regs)
    {
        terminal_print("TCPD: no GPIO info\n");
        return;
    }

    if (!regs->tcpd_gpio_valid && regs->tcpd_gpio_pin == 0u)
    {
        terminal_print("TCPD: no GPIO info\n");
        return;
    }

    terminal_print("TCPD GPIO pulse pin:");
    terminal_print_hex32(regs->tcpd_gpio_pin);
    terminal_print(" flags:");
    terminal_print_hex32(regs->tcpd_gpio_flags);
    terminal_print("\n");

    (void)gpio_init();
    (void)gpio_set_output(regs->tcpd_gpio_pin);

    /*
      Conservative guess:
      bit0 often distinguishes active-high vs active-low in GPIO resource flags.
      If your platform defines this differently, adjust here.
    */
    if (regs->tcpd_gpio_flags & 0x1u)
    {
        active = GPIO_VALUE_LOW;
        inactive = GPIO_VALUE_HIGH;
    }
    else
    {
        active = GPIO_VALUE_HIGH;
        inactive = GPIO_VALUE_LOW;
    }

    (void)gpio_write(regs->tcpd_gpio_pin, active);
    delay_ms_approx(20u);
    (void)gpio_write(regs->tcpd_gpio_pin, inactive);
    delay_ms_approx(80u);
}

static void tcpd_gpio_drive_pair(const hidi2c_acpi_regs *regs,
                                 uint32_t first,
                                 uint32_t second,
                                 uint32_t first_ms,
                                 uint32_t second_ms,
                                 const char *tag)
{
    if (!regs || !regs->tcpd_gpio_valid)
        return;

    terminal_print("TCPD GPIO seq: ");
    terminal_print(tag);
    terminal_print(" pin:");
    terminal_print_hex32(regs->tcpd_gpio_pin);
    terminal_print("\n");

    (void)gpio_init();
    (void)gpio_set_output(regs->tcpd_gpio_pin);

    (void)gpio_write(regs->tcpd_gpio_pin, first);
    delay_ms_approx(first_ms);

    (void)gpio_write(regs->tcpd_gpio_pin, second);
    delay_ms_approx(second_ms);
}

static void tcpd_gpio_pulse_active_low(const hidi2c_acpi_regs *regs)
{
    tcpd_gpio_drive_pair(regs,
                         GPIO_VALUE_LOW,
                         GPIO_VALUE_HIGH,
                         20u,
                         120u,
                         "active-low");
}

static void tcpd_gpio_pulse_active_high(const hidi2c_acpi_regs *regs)
{
    tcpd_gpio_drive_pair(regs,
                         GPIO_VALUE_HIGH,
                         GPIO_VALUE_LOW,
                         20u,
                         120u,
                         "active-high");
}

static int buf_any_nonzero(const uint8_t *buf, uint32_t len)
{
    if (!buf)
        return 0;

    for (uint32_t i = 0; i < len; ++i)
    {
        if (buf[i] != 0u)
            return 1;
    }

    return 0;
}

static void print_probe8(const char *tag, uint16_t reg, const uint8_t *rx)
{
    if (!g_hidi2c_debug)
        return;

    terminal_print(tag);
    terminal_print(" reg:");
    terminal_print_hex32(reg);
    terminal_print(" b:");
    terminal_print_hex8(rx[0]);
    terminal_print(" ");
    terminal_print_hex8(rx[1]);
    terminal_print(" ");
    terminal_print_hex8(rx[2]);
    terminal_print(" ");
    terminal_print_hex8(rx[3]);
    terminal_print(" ");
    terminal_print_hex8(rx[4]);
    terminal_print(" ");
    terminal_print_hex8(rx[5]);
    terminal_print(" ");
    terminal_print_hex8(rx[6]);
    terminal_print(" ");
    terminal_print_hex8(rx[7]);
    terminal_print("\n");
}

static void desc_parse(hidi2c_desc *d, const uint8_t *buf, uint32_t len)
{
    if (!d || !buf || len < 30)
        return;

    d->wHIDDescLength = rd16(buf + 0x00);
    d->bcdVersion = rd16(buf + 0x02);
    d->wReportDescLength = rd16(buf + 0x04);
    d->wReportDescRegister = rd16(buf + 0x06);
    d->wInputRegister = rd16(buf + 0x08);
    d->wMaxInputLength = rd16(buf + 0x0A);
    d->wOutputRegister = rd16(buf + 0x0C);
    d->wMaxOutputLength = rd16(buf + 0x0E);
    d->wCommandRegister = rd16(buf + 0x10);
    d->wDataRegister = rd16(buf + 0x12);
    d->wVendorID = rd16(buf + 0x14);
    d->wProductID = rd16(buf + 0x16);
    d->wVersionID = rd16(buf + 0x18);
    d->valid = 1;
}

static int desc_looks_valid(const hidi2c_desc *d)
{
    if (!d || !d->valid)
        return 0;

    if (d->wHIDDescLength < 30u || d->wHIDDescLength > 256u)
        return 0;
    /* accept all HID 1.x versions */
    if ((d->bcdVersion & 0xFF00u) != 0x0100u)
        return 0;
    if (d->wReportDescLength == 0u)
        return 0;
    if (d->wInputRegister == 0u)
        return 0;
    if (d->wMaxInputLength < 2u)
        return 0;
    if (d->wCommandRegister == 0u || d->wDataRegister == 0u)
        return 0;

    return 1;
}

static uint32_t hidi2c_encode_command(uint8_t *buf, uint8_t opcode, uint8_t report_type, uint8_t report_id)
{
    uint32_t length = 0;

    if (report_id < 0x0Fu)
    {
        buf[length++] = (uint8_t)((report_type << 4) | report_id);
        buf[length++] = opcode;
    }
    else
    {
        buf[length++] = (uint8_t)((report_type << 4) | 0x0Fu);
        buf[length++] = opcode;
        buf[length++] = report_id;
    }

    return length;
}

static int hidi2c_try_desc_reg_split(hidi2c_device *dev, uint16_t reg)
{
    uint8_t tx[2];
    uint8_t rx[30];
    int rc;

    for (uint32_t i = 0; i < sizeof(rx); ++i)
        rx[i] = 0;

    wr16(tx, reg);
    rc = i2c1_bus_write_read(dev->i2c_addr_7bit, tx, 2, rx, sizeof(rx));

    terminal_print(dev->name);
    terminal_print(" split reg:");
    terminal_print_hex32(reg);
    terminal_print(" rc:");
    terminal_print_hex32((uint32_t)rc);
    terminal_print(" b0:");
    terminal_print_hex8(rx[0]);
    terminal_print(" b1:");
    terminal_print_hex8(rx[1]);
    terminal_print(" b2:");
    terminal_print_hex8(rx[2]);
    terminal_print(" b3:");
    terminal_print_hex8(rx[3]);
    terminal_print("\n");

    if (rc != 0)
        return -1;

    desc_parse(&dev->desc, rx, sizeof(rx));
    if (!desc_looks_valid(&dev->desc))
        return -1;

    dev->hid_desc_reg = reg;
    return 0;
}

static int hidi2c_try_desc_reg_combined(hidi2c_device *dev, uint16_t reg)
{
    uint8_t tx[2];
    uint8_t rx[30];
    int rc;

    for (uint32_t i = 0; i < sizeof(rx); ++i)
        rx[i] = 0;

    wr16(tx, reg);
    rc = i2c1_bus_write_read_combined(dev->i2c_addr_7bit, tx, 2, rx, sizeof(rx));

    terminal_print(dev->name);
    terminal_print(" comb reg:");
    terminal_print_hex32(reg);
    terminal_print(" rc:");
    terminal_print_hex32((uint32_t)rc);
    terminal_print(" b0:");
    terminal_print_hex8(rx[0]);
    terminal_print(" b1:");
    terminal_print_hex8(rx[1]);
    terminal_print(" b2:");
    terminal_print_hex8(rx[2]);
    terminal_print(" b3:");
    terminal_print_hex8(rx[3]);
    terminal_print("\n");

    if (rc != 0)
        return -1;

    desc_parse(&dev->desc, rx, sizeof(rx));
    if (!desc_looks_valid(&dev->desc))
        return -1;

    dev->hid_desc_reg = reg;
    return 0;
}

static int hidi2c_try_desc_reg_split_endian(hidi2c_device *dev, uint16_t reg, int big_endian)
{
    uint8_t tx[2];
    uint8_t rx[30];
    int rc;

    for (uint32_t i = 0; i < sizeof(rx); ++i)
        rx[i] = 0;

    if (big_endian)
        wr16be(tx, reg);
    else
        wr16(tx, reg);

    rc = i2c1_bus_write_read(dev->i2c_addr_7bit, tx, 2, rx, sizeof(rx));

    terminal_print(dev->name);
    terminal_print(big_endian ? " split-BE reg:" : " split-LE reg:");
    terminal_print_hex32(reg);
    terminal_print(" rc:");
    terminal_print_hex32((uint32_t)rc);
    terminal_print(" b0:");
    terminal_print_hex8(rx[0]);
    terminal_print(" b1:");
    terminal_print_hex8(rx[1]);
    terminal_print(" b2:");
    terminal_print_hex8(rx[2]);
    terminal_print(" b3:");
    terminal_print_hex8(rx[3]);
    terminal_print("\n");

    if (rc != 0)
        return -1;

    desc_parse(&dev->desc, rx, sizeof(rx));
    if (!desc_looks_valid(&dev->desc))
        return -1;

    dev->hid_desc_reg = reg;
    return 0;
}

static int hidi2c_try_desc_reg_read_only_after_pointer(hidi2c_device *dev, uint16_t reg, int big_endian)
{
    uint8_t tx[2];
    uint8_t rx[30];
    int rc;

    if (!dev)
        return -1;

    for (uint32_t i = 0; i < sizeof(rx); ++i)
        rx[i] = 0;

    if (big_endian)
        wr16be(tx, reg);
    else
        wr16(tx, reg);

    /*
      First set the internal register pointer with a normal write.
      Then do a plain read, instead of a repeated-start write_read.
    */
    rc = i2c1_bus_write(dev->i2c_addr_7bit, tx, 2u);
    if (rc != 0)
    {
        terminal_print(dev->name);
        terminal_print(big_endian ? " ptrwr-BE reg:" : " ptrwr-LE reg:");
        terminal_print_hex32(reg);
        terminal_print(" rc:");
        terminal_print_hex32((uint32_t)rc);
        terminal_print("\n");
        return -1;
    }

    delay_ms_approx(2u);

    rc = i2c1_bus_read(dev->i2c_addr_7bit, rx, sizeof(rx));

    terminal_print(dev->name);
    terminal_print(big_endian ? " rdonly-BE reg:" : " rdonly-LE reg:");
    terminal_print_hex32(reg);
    terminal_print(" rc:");
    terminal_print_hex32((uint32_t)rc);
    terminal_print(" b0:");
    terminal_print_hex8(rx[0]);
    terminal_print(" b1:");
    terminal_print_hex8(rx[1]);
    terminal_print(" b2:");
    terminal_print_hex8(rx[2]);
    terminal_print(" b3:");
    terminal_print_hex8(rx[3]);
    terminal_print("\n");

    if (rc != 0)
        return -1;

    desc_parse(&dev->desc, rx, sizeof(rx));
    if (!desc_looks_valid(&dev->desc))
        return -1;

    dev->hid_desc_reg = reg;
    return 0;
}

static int hidi2c_try_desc_reg_combined_endian(hidi2c_device *dev, uint16_t reg, int big_endian)
{
    uint8_t tx[2];
    uint8_t rx[30];
    int rc;

    for (uint32_t i = 0; i < sizeof(rx); ++i)
        rx[i] = 0;

    if (big_endian)
        wr16be(tx, reg);
    else
        wr16(tx, reg);

    rc = i2c1_bus_write_read_combined(dev->i2c_addr_7bit, tx, 2, rx, sizeof(rx));

    terminal_print(dev->name);
    terminal_print(big_endian ? " comb-BE reg:" : " comb-LE reg:");
    terminal_print_hex32(reg);
    terminal_print(" rc:");
    terminal_print_hex32((uint32_t)rc);
    terminal_print(" b0:");
    terminal_print_hex8(rx[0]);
    terminal_print(" b1:");
    terminal_print_hex8(rx[1]);
    terminal_print(" b2:");
    terminal_print_hex8(rx[2]);
    terminal_print(" b3:");
    terminal_print_hex8(rx[3]);
    terminal_print("\n");

    if (rc != 0)
        return -1;

    desc_parse(&dev->desc, rx, sizeof(rx));
    if (!desc_looks_valid(&dev->desc))
        return -1;

    dev->hid_desc_reg = reg;
    return 0;
}

static int hidi2c_scan_for_desc(hidi2c_device *dev)
{
    uint8_t tx[2];
    uint8_t rx[30];
    uint16_t reg;

    if (!dev)
        return -1;

    i2c1_bus_set_quiet(1);

    for (reg = 0x0000u; reg <= 0x0400u; reg += 2u)
    {
        hidi2c_desc d;
        int rc;

        wr16(tx, reg);

        for (uint32_t i = 0; i < sizeof(rx); ++i)
            rx[i] = 0;

        rc = i2c1_bus_write_read(dev->i2c_addr_7bit, tx, 2u, rx, sizeof(rx));
        if (rc == 0 && buf_any_nonzero(rx, sizeof(rx)))
        {
            print_probe8("scan split", reg, rx);

            d.valid = 0;
            desc_parse(&d, rx, sizeof(rx));

            if (desc_looks_valid(&d))
            {
                dev->desc = d;
                dev->hid_desc_reg = reg;
                i2c1_bus_set_quiet(0);
                terminal_print("FOUND HID DESC split @ ");
                terminal_print_hex32(reg);
                terminal_print("\n");
                return 0;
            }
        }

        for (uint32_t i = 0; i < sizeof(rx); ++i)
            rx[i] = 0;

        rc = i2c1_bus_write_read_combined(dev->i2c_addr_7bit, tx, 2u, rx, sizeof(rx));
        if (rc == 0 && buf_any_nonzero(rx, sizeof(rx)))
        {
            print_probe8("scan comb", reg, rx);

            d.valid = 0;
            desc_parse(&d, rx, sizeof(rx));

            if (desc_looks_valid(&d))
            {
                dev->desc = d;
                dev->hid_desc_reg = reg;
                i2c1_bus_set_quiet(0);
                terminal_print("FOUND HID DESC comb @ ");
                terminal_print_hex32(reg);
                terminal_print("\n");
                return 0;
            }
        }
    }

    i2c1_bus_set_quiet(0);
    return -1;
}

static int hidi2c_fetch_desc_touchpad(hidi2c_device *dev)
{
    uint16_t reg;
    int rc;

    if (!dev)
        return -1;

    /*
      First try the obvious ACPI/fallback locations.
    */
    rc = hidi2c_try_desc_reg_read_only_after_pointer(dev, TCPD_DESC_REG_ACPI, 0);
    if (rc == 0)
        return 0;

    rc = hidi2c_try_desc_reg_read_only_after_pointer(dev, TCPD_DESC_REG_ACPI, 1);
    if (rc == 0)
        return 0;

    rc = hidi2c_try_desc_reg_read_only_after_pointer(dev, HIDI2C_DESC_REG_FALLBACK, 0);
    if (rc == 0)
        return 0;

    rc = hidi2c_try_desc_reg_read_only_after_pointer(dev, HIDI2C_DESC_REG_FALLBACK, 1);
    if (rc == 0)
        return 0;

    /*
      Then do a tight read-only scan.
      Keep it small for now: 0x0000..0x0040, step 2.
    */
    terminal_print("TCPD scan rdonly start\n");

    for (reg = 0x0000u; reg <= 0x0040u; reg += 2u)
    {
        rc = hidi2c_try_desc_reg_read_only_after_pointer(dev, reg, 0);
        if (rc == 0)
        {
            terminal_print("TCPD found desc LE @ ");
            terminal_print_hex32(reg);
            terminal_print("\n");
            return 0;
        }

        rc = hidi2c_try_desc_reg_read_only_after_pointer(dev, reg, 1);
        if (rc == 0)
        {
            terminal_print("TCPD found desc BE @ ");
            terminal_print_hex32(reg);
            terminal_print("\n");
            return 0;
        }
    }

    terminal_print("TCPD scan rdonly no hit\n");
    return -1;
}

static int hidi2c_fetch_desc_touchpad_retry(hidi2c_device *dev, uint32_t tries)
{
    for (uint32_t i = 0; i < tries; ++i)
    {
        if (hidi2c_fetch_desc_touchpad(dev) == 0)
            return 0;
            
        if (g_hidi2c_debug)
        {
            terminal_print("TCPD retry:");
            terminal_print_hex32(i);
            terminal_print("\n");
        }

        short_delay(3000000u);
    }

    return -1;
}

static int hidi2c_fetch_desc_keyboard(hidi2c_device *dev)
{
    if (!dev)
        return -1;

    /* common Lenovo keyboard locations */

    if (hidi2c_try_desc_reg_split(dev, 0x0020u) == 0)
        return 0;
    if (hidi2c_try_desc_reg_combined(dev, 0x0020u) == 0)
        return 0;

    if (hidi2c_try_desc_reg_split(dev, 0x0010u) == 0)
        return 0;
    if (hidi2c_try_desc_reg_combined(dev, 0x0010u) == 0)
        return 0;

    if (hidi2c_try_desc_reg_split(dev, 0x0001u) == 0)
        return 0;
    if (hidi2c_try_desc_reg_combined(dev, 0x0001u) == 0)
        return 0;

    if (hidi2c_try_desc_reg_split(dev, 0x0000u) == 0)
        return 0;
    if (hidi2c_try_desc_reg_combined(dev, 0x0000u) == 0)
        return 0;

    return -1;
}

static int hidi2c_set_power(hidi2c_device *dev, uint8_t power_state)
{
    uint8_t cmd[4];
    uint32_t len = 0;

    if (!dev || !dev->desc.valid)
        return -1;

    wr16(cmd + len, dev->desc.wCommandRegister);
    len += 2;
    len += hidi2c_encode_command(cmd + len, I2C_HID_OPCODE_SET_POWER, 0u, power_state);

    terminal_print(dev->name);
    terminal_print(" pwr:");
    terminal_print_hex8(power_state);
    terminal_print("\n");

    return i2c1_bus_write(dev->i2c_addr_7bit, cmd, len);
}

static int hidi2c_reset(hidi2c_device *dev)
{
    uint8_t cmd[4];
    uint32_t len = 0;

    if (!dev || !dev->desc.valid)
        return -1;

    wr16(cmd + len, dev->desc.wCommandRegister);
    len += 2;
    len += hidi2c_encode_command(cmd + len, I2C_HID_OPCODE_RESET, 0u, 0u);

    terminal_print(dev->name);
    terminal_print(" reset\n");

    return i2c1_bus_write(dev->i2c_addr_7bit, cmd, len);
}

static int hidi2c_post_desc_init(hidi2c_device *dev)
{
    if (!dev || !dev->desc.valid)
        return -1;

    if (hidi2c_set_power(dev, I2C_HID_PWR_ON) != 0)
        return -1;

    short_delay(3000000u);

    if (hidi2c_reset(dev) != 0)
        return -1;

    short_delay(3000000u);
    return 0;
}

static int hidi2c_read_input(hidi2c_device *dev)
{
    uint8_t rx[128];
    uint32_t want;
    uint16_t total;

    if (!dev || !dev->online || !dev->desc.valid)
        return -1;

    want = dev->desc.wMaxInputLength;
    if (want < 2u)
        want = 2u;
    if (want > sizeof(rx))
        want = sizeof(rx);

    if (i2c1_bus_read(dev->i2c_addr_7bit, rx, want) != 0)
        return -1;

    total = rd16(rx + 0);
    if (total == 0u)
    {
        terminal_print(dev->name);
        terminal_print(" empty pkt\n");
        return 0;
    }
    if (total < 2u)
        return 1;
    if (total > want)
        total = (uint16_t)want;

    dev->last_report.len = (uint32_t)(total - 2u);
    if (dev->last_report.len > sizeof(dev->last_report.data))
        dev->last_report.len = sizeof(dev->last_report.data);

    for (uint32_t i = 0; i < dev->last_report.len; ++i)
        dev->last_report.data[i] = rx[2u + i];

    dev->last_report.available = (dev->last_report.len != 0u);

    if (dev->last_report.available)
    {
        terminal_print(dev->name);
        terminal_print(" pkt len:");
        terminal_print_hex32(dev->last_report.len);
        terminal_print(" b0:");
        terminal_print_hex8(dev->last_report.data[0]);
        terminal_print("\n");
    }

    return 0;
}

void i2c1_hidi2c_init(uint64_t rsdp_phys)
{
    hidi2c_acpi_regs regs;
    int have_regs = 0;

    g_kbd.name = "ECKB";
    g_kbd.i2c_addr_7bit = 0x3A;
    g_kbd.gpio_pin = 0u;
    g_kbd.hid_desc_reg = 0u;
    g_kbd.online = 0;
    g_kbd.last_report.available = 0;
    g_kbd.last_report.len = 0;
    g_kbd.desc.valid = 0;

    g_tpd.name = "TCPD";
    g_tpd.i2c_addr_7bit = TCPD_ADDR_ACPI;
    g_tpd.gpio_pin = 0u;
    g_tpd.hid_desc_reg = 0u;
    g_tpd.online = 0;
    g_tpd.last_report.available = 0;
    g_tpd.last_report.len = 0;
    g_tpd.desc.valid = 0;

    if (i2c1_bus_init() != 0)
    {
        terminal_warn("i2c1 bus init failed");
        g_bus_ready = 0;
        return;
    }

    g_bus_ready = 1;

    if (acpi_hidi2c_get_regs_from_rsdp(rsdp_phys, &regs) == 0)
    {
        terminal_set_loud();

        have_regs = 1;

        if (regs.eckb_addr)
            g_kbd.i2c_addr_7bit = regs.eckb_addr;
        if (regs.tcpd_addr)
            g_tpd.i2c_addr_7bit = regs.tcpd_addr;
        if (regs.tcpd_desc_reg)
            g_tpd.hid_desc_reg = regs.tcpd_desc_reg;

        terminal_print("ACPI ECKB addr:");
        terminal_print_hex8(g_kbd.i2c_addr_7bit);
        terminal_print(" reg:");
        terminal_print_hex32(regs.eckb_desc_reg);
        terminal_print(" trust:");
        terminal_print_hex8(regs.eckb_desc_trusted);
        terminal_print("\n");

        terminal_print("ACPI TCPD addr:");
        terminal_print_hex8(g_tpd.i2c_addr_7bit);
        terminal_print(" reg:");
        terminal_print_hex32(regs.tcpd_desc_reg);
        terminal_print(" trust:");
        terminal_print_hex8(regs.tcpd_desc_trusted);
        terminal_print("\n");

        if (regs.tcpd_gpio_valid)
        {
            terminal_print("ACPI TCPD gpio pin:");
            terminal_print_hex32(regs.tcpd_gpio_pin);
            terminal_print(" flags:");
            terminal_print_hex32(regs.tcpd_gpio_flags);
            terminal_print("\n");
        }
    }
    else
    {
        terminal_set_loud();
    }

    /* Keyboard: only trust ACPI if probe says it is trusted */
    if (have_regs && regs.have_eckb && regs.eckb_desc_trusted)
    {
        if (hidi2c_try_desc_reg_split(&g_kbd, regs.eckb_desc_reg) == 0 ||
            hidi2c_try_desc_reg_combined(&g_kbd, regs.eckb_desc_reg) == 0)
        {
            if (hidi2c_post_desc_init(&g_kbd) == 0)
            {
                g_kbd.online = 1;
                terminal_success("ECKB HID-over-I2C online");
            }
        }
        else
        {
            terminal_warn("ECKB trusted ACPI reg failed");
        }
    }
    else
    {
        terminal_warn("ECKB not trusted HIDI2C path; skipping");
    }

    if (have_regs && regs.have_tcpd)
    {
        int ok = 0;

        terminal_print("TCPD ACPI full quiet init + no-GPIO wake\n");

        terminal_print("TCPD ACPI desc reg:");
        terminal_print_hex32(regs.tcpd_desc_reg);
        terminal_print(" trusted:");
        terminal_print_hex8(regs.tcpd_desc_trusted);
        terminal_print(" dsm_valid:");
        terminal_print_hex8(regs.tcpd_dsm_valid);
        terminal_print(" dsm_len:");
        terminal_print_hex32(regs.tcpd_dsm_len);
        terminal_print(" gio0_dsm_valid:");
        terminal_print_hex8(regs.tcpd_gio0_dsm_valid);
        terminal_print(" gio0_dsm_len:");
        terminal_print_hex32(regs.tcpd_gio0_dsm_len);
        terminal_print("\n");

        /*
          If _DSM is not really trusted, do not pretend 0x20 is reliable.
          We can still probe, but we should know we are in heuristic mode.
        */
        if (!regs.tcpd_desc_trusted)
            terminal_warn("TCPD _DSM not strongly trusted; desc reg is heuristic");

        /*
          We now know:
          - bus transport is fine
          - pointer write + read is fine
          - scan 0x0000..0x0040 finds no nonzero descriptor
          So the next best guess is that TCPD needs more of its ACPI
          bring-up sequence before it will expose HID registers.

          Keep it quiet so we do not flood the terminal again.
        */
        g_aml_gabl = 0;
        g_aml_lids = 0;
        g_aml_lidb = 0;
        g_aml_lidr = 0;

        terminal_set_quiet();

        tcpd_try_sta_from_acpi(&regs);
        tcpd_try_ini_from_acpi(&regs);
        tcpd_try_gio0_reg_from_acpi(&regs);
        tcpd_try_ps0_from_acpi(&regs);

        terminal_set_loud();

        delay_ms_approx(80u);

        hidi2c_touchpad_wake_probe(&g_tpd);
        delay_ms_approx(120u);

        if (hidi2c_fetch_desc_touchpad_retry(&g_tpd, 1u) == 0)
            ok = 1;

        if (ok)
        {
            if (hidi2c_post_desc_init(&g_tpd) == 0)
            {
                g_tpd.online = 1;
                terminal_success("TCPD HID-over-I2C online");
            }
            else
            {
                terminal_warn("TCPD post-desc init failed");
            }
        }
        else
        {
            terminal_warn("TCPD no valid HID descriptor after ACPI full quiet init + no-GPIO wake");
        }
    }
}

void i2c1_hidi2c_poll(void)
{
    if (!g_bus_ready)
        return;

    if (g_tpd.online)
        (void)hidi2c_read_input(&g_tpd);
    if (g_kbd.online)
        (void)hidi2c_read_input(&g_kbd);
}

const hidi2c_device *i2c1_hidi2c_keyboard(void)
{
    return &g_kbd;
}

const hidi2c_device *i2c1_hidi2c_touchpad(void)
{
    return &g_tpd;
}