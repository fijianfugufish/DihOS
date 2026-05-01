#include "i2c/i2c1_hidi2c.h"
#include "terminal/terminal_api.h"
#include "hardware_probes/acpi_probe_hidi2c_ready.h"
#include "acpi/aml_tiny.h"
#include "gpio/gpio.h"
#include "kwrappers/string.h"
#include "asm/asm.h"
#include <stdint.h>

#define TCPD_ADDR_ACPI 0x2Cu
#define TCPD_DESC_REG_ACPI 0x0020u
#define HIDI2C_DESC_REG_FALLBACK 0x0000u

#define I2C_HID_OPCODE_RESET 0x01u
#define I2C_HID_OPCODE_GET_REPORT 0x02u
#define I2C_HID_OPCODE_SET_REPORT 0x03u
#define I2C_HID_OPCODE_SET_POWER 0x08u
#define I2C_HID_PWR_ON 0x00u
#define I2C_HID_REPORT_TYPE_OUTPUT 0x02u
#define I2C_HID_REPORT_TYPE_FEATURE 0x03u
#define TCPD_PTP_INPUT_MODE_MOUSE 0x00u
#define TCPD_PTP_INPUT_MODE_TOUCHPAD 0x03u

#define TCPD_GPIO_TEST_ENABLE 0u

static hidi2c_device g_kbd;
static hidi2c_device g_tpd;
static hidi2c_device g_probe_dev;
static char g_kbd_name[5] = "ECKB";
static char g_tpd_name[5] = "TCPD";
static char g_probe_name[5] = "HID?";
static uint8_t g_bus_ready = 0;
static uint32_t g_aml_gpio_pin = 0u;

static uint8_t g_hidi2c_debug = 0;
static uint16_t g_tcpd_desc_reg_hint = 0u;

typedef struct
{
    uint8_t valid;
    uint8_t has_report_id;
    uint8_t report_id;
    uint16_t value_bits;
    uint8_t value_size;
    uint16_t report_bits;
} tcpd_feature_value_layout;

typedef struct
{
    uint8_t valid;
    uint8_t has_report_id;
    uint8_t report_id;
    uint16_t surface_bits;
    uint8_t surface_size;
    uint16_t button_bits;
    uint8_t button_size;
    uint16_t report_bits;
} tcpd_selective_layout;

static tcpd_feature_value_layout g_tcpd_input_mode = {0};
static tcpd_selective_layout g_tcpd_selective = {0};

static void hidi2c_clear_report_desc(hidi2c_device *dev);

static void hidi2c_copy_name4(char dst[5], const char *src, const char *fallback)
{
    uint32_t i;

    if (!dst)
        return;

    if (!src || !src[0])
        src = fallback ? fallback : "HID?";

    for (i = 0; i < 4u && src[i]; ++i)
        dst[i] = src[i];

    for (; i < 4u; ++i)
        dst[i] = 0;

    dst[4] = 0;
}

static void hidi2c_init_device_slot(hidi2c_device *dev,
                                    char name_storage[5],
                                    const char *name,
                                    uint8_t addr,
                                    uint32_t gpio_pin,
                                    uint16_t hid_desc_reg)
{
    if (!dev || !name_storage)
        return;

    hidi2c_copy_name4(name_storage, name, "HID?");

    dev->name = name_storage;
    dev->i2c_addr_7bit = addr;
    dev->gpio_pin = gpio_pin;
    dev->hid_desc_reg = hid_desc_reg;
    dev->online = 0u;
    dev->last_report.available = 0u;
    dev->last_report.len = 0u;
    memset(&dev->desc, 0, sizeof(dev->desc));
    hidi2c_clear_report_desc(dev);
}

static void hidi2c_adopt_device(hidi2c_device *dst,
                                char name_storage[5],
                                const hidi2c_device *src)
{
    if (!dst || !src || !name_storage)
        return;

    *dst = *src;
    hidi2c_copy_name4(name_storage, src->name, "HID?");
    dst->name = name_storage;
}

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
        asm_relax();
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

static uint32_t hid_item_u32(const uint8_t *p, uint8_t nbytes)
{
    uint32_t v = 0u;

    if (!p)
        return 0u;

    if (nbytes > 0u)
        v |= (uint32_t)p[0];
    if (nbytes > 1u)
        v |= (uint32_t)p[1] << 8;
    if (nbytes > 2u)
        v |= (uint32_t)p[2] << 16;
    if (nbytes > 3u)
        v |= (uint32_t)p[3] << 24;

    return v;
}

static uint32_t hid_usage_for_index(const uint32_t *usages, uint8_t usage_count,
                                    uint8_t have_usage_range, uint32_t usage_min, uint32_t usage_max,
                                    uint32_t index)
{
    if (index < usage_count)
        return usages[index];

    if (have_usage_range && usage_max >= usage_min)
    {
        uint32_t range_count = usage_max - usage_min + 1u;
        if (index < range_count)
            return usage_min + index;
    }

    return 0u;
}

static void hid_write_bits(uint8_t *data, uint32_t len, uint16_t bit_off, uint8_t bit_size, uint32_t value)
{
    if (!data || bit_size == 0u || bit_size > 32u)
        return;

    for (uint32_t i = 0; i < bit_size; ++i)
    {
        uint32_t abs_bit = (uint32_t)bit_off + i;
        uint32_t byte_idx = abs_bit >> 3;
        uint32_t bit_idx = abs_bit & 7u;

        if (byte_idx >= len)
            break;

        if ((value >> i) & 1u)
            data[byte_idx] |= (uint8_t)(1u << bit_idx);
        else
            data[byte_idx] &= (uint8_t)~(1u << bit_idx);
    }
}

static void tcpd_clear_feature_layouts(void)
{
    g_tcpd_input_mode = (tcpd_feature_value_layout){0};
    g_tcpd_selective = (tcpd_selective_layout){0};
}

static uint8_t tcpd_desc_hint_plausible(uint64_t v)
{
    if (v == 0u)
        return 0u;

    /*
      Keep the hint bounded and aligned so a bad AML return value does not turn
      into a huge blind register search.
    */
    if (v > 0x0400u)
        return 0u;

    if ((v & 1u) != 0u)
        return 0u;

    return 1u;
}

static uint8_t eckb_desc_hint_plausible(uint64_t v)
{
    if (v == 0u || v == 0xFFFFFFFFu)
        return 0u;

    if (v > 0xFFFFu)
        return 0u;

    return 1u;
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
    

    return rc;
}

static uint64_t g_aml_lids = 0;
static uint64_t g_aml_lidr = 0;
static uint64_t g_aml_gabl = 0;
static uint64_t g_aml_lidb = 0;
static uint64_t g_aml_t0 = 0;
static uint64_t g_aml_t1 = 0;

static void tcpd_drive_gpio_wake_edge(uint64_t value)
{
    int rc;
    uint32_t gpio_pin = g_aml_gpio_pin ? g_aml_gpio_pin : g_tpd.gpio_pin;

    if (!g_aml_gabl || gpio_pin == 0u)
        return;

    rc = gpio_init();
    if (rc != 0)
    {
        terminal_print("AML GPIO gpio_init rc:");
        terminal_print_hex32((uint32_t)rc);
        terminal_print("\n");
        return;
    }

    rc = gpio_set_output(gpio_pin);
    if (rc != 0)
    {
        terminal_print("AML GPIO gpio_set_output rc:");
        terminal_print_hex32((uint32_t)rc);
        terminal_print(" pin:");
        terminal_print_hex32(gpio_pin);
        terminal_print("\n");
        return;
    }

    if (value)
    {
        (void)gpio_write(gpio_pin, GPIO_VALUE_LOW);
        delay_us_approx(1000u);
        (void)gpio_write(gpio_pin, GPIO_VALUE_HIGH);
        delay_ms_approx(10u);

        terminal_print("AML kick GPIO pin:");
        terminal_print_hex32(gpio_pin);
        terminal_print("\n");
        return;
    }

    (void)gpio_write(gpio_pin, GPIO_VALUE_LOW);
    delay_us_approx(1000u);

    terminal_print("AML drive GPIO low pin:");
    terminal_print_hex32(gpio_pin);
    terminal_print("\n");
}

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

    if (name && acpi_name_has(name, "GIO0._T_0"))
    {
        *out = g_aml_t0;
        return 0;
    }

    if (name && acpi_name_has(name, "GIO0._T_1"))
    {
        *out = g_aml_t1;
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

        if (value && (g_aml_lidr || g_aml_lidb))
            tcpd_drive_gpio_wake_edge(g_aml_lidr ? g_aml_lidr : g_aml_lidb);

        return 0;
    }

    if (acpi_name_has(name, "LID0.LIDB"))
    {
        g_aml_lidb = value;

        terminal_print("AML write LIDB=");
        terminal_print_hex32((uint32_t)value);

        tcpd_drive_gpio_wake_edge(value);
        
        return 0;
    }

    if (acpi_name_has(name, "GIO0.LIDR"))
    {
        g_aml_lidr = value;

        terminal_print("AML write LIDR=");
        terminal_print_hex32((uint32_t)value);

        tcpd_drive_gpio_wake_edge(value);
        
        return 0;
    }

    if (acpi_name_has(name, "GIO0._T_0"))
    {
        g_aml_t0 = value;

        terminal_print("AML write _T_0=");
        terminal_print_hex32((uint32_t)value);

        return 0;
    }

    if (acpi_name_has(name, "GIO0._T_1"))
    {
        g_aml_t1 = value;

        terminal_print("AML write _T_1=");
        terminal_print_hex32((uint32_t)value);

        return 0;
    }

    terminal_print("AML ignored write: ");
    terminal_print(name);

    return 0;
}

static void tcpd_aml_log(void *user, const char *msg)
{
    (void)user;
    terminal_print("AML: ");
    terminal_print(msg);
    
}

static void tcpd_try_method_from_acpi_ex(const char *tag,
                                         const char *scope_prefix,
                                         const uint8_t *body,
                                         uint16_t len,
                                         uint8_t valid,
                                         const uint64_t *args,
                                         uint32_t arg_count)
{
    aml_tiny_method m = {0};
    aml_tiny_host h = {0};
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
    m.use_typed_args = 0u;

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

    rc = aml_tiny_trace_names(&m, &h);
    terminal_print("AML trace rc:");
    terminal_print_hex32((uint32_t)rc);

    rc = aml_tiny_exec(&m, &h, &ret);
    terminal_print("AML exec rc:");
    terminal_print_hex32((uint32_t)rc);
    terminal_print(" ret:");
    terminal_print_hex32((uint32_t)ret);
}

static int aml_raw_pkglen(const uint8_t *p, const uint8_t *end,
                          uint32_t *pkg_len_out, uint32_t *pkg_bytes_out)
{
    uint8_t lead;
    uint32_t follow_count;
    uint32_t len;
    uint32_t i;

    if (!p || !end || !pkg_len_out || !pkg_bytes_out || p >= end)
        return -1;

    lead = p[0];
    follow_count = (uint32_t)((lead >> 6) & 0x03u);

    if (p + 1u + follow_count > end)
        return -1;

    len = (uint32_t)(lead & 0x0Fu);

    for (i = 0; i < follow_count; ++i)
        len |= ((uint32_t)p[1u + i]) << (4u + 8u * i);

    *pkg_len_out = len;
    *pkg_bytes_out = 1u + follow_count;
    return 0;
}

static int aml_find_method_body_in_blob(const uint8_t *aml,
                                        uint32_t aml_len,
                                        const char *name4,
                                        const uint8_t **body_out,
                                        uint16_t *body_len_out)
{
    uint32_t i;

    if (!aml || !name4 || !body_out || !body_len_out)
        return -1;

    *body_out = 0;
    *body_len_out = 0;

    for (i = 0; i + 6u < aml_len; ++i)
    {
        uint32_t pkg_len = 0;
        uint32_t pkg_bytes = 0;
        uint32_t name_off;
        uint32_t flags_off;
        uint32_t body_start;
        uint32_t meth_end;

        if (aml[i] != 0x14u) /* MethodOp */
            continue;

        if (aml_raw_pkglen(aml + i + 1u, aml + aml_len, &pkg_len, &pkg_bytes) != 0)
            continue;

        name_off = i + 1u + pkg_bytes;
        flags_off = name_off + 4u;
        body_start = flags_off + 1u;
        meth_end = i + 1u + pkg_bytes + pkg_len;

        if (name_off + 4u > aml_len)
            continue;
        if (meth_end > aml_len)
            meth_end = aml_len;
        if (body_start > meth_end)
            continue;

        if (aml[name_off + 0] != (uint8_t)name4[0] ||
            aml[name_off + 1] != (uint8_t)name4[1] ||
            aml[name_off + 2] != (uint8_t)name4[2] ||
            aml[name_off + 3] != (uint8_t)name4[3])
            continue;

        *body_out = aml + body_start;
        *body_len_out = (uint16_t)(meth_end - body_start);
        return 0;
    }

    return -1;
}

static int tcpd_run_dsm_typed_guid(const char *tag,
                                   const char *scope_prefix,
                                   const uint8_t *body,
                                   uint16_t len,
                                   uint8_t valid,
                                   const uint8_t *guid16,
                                   uint64_t rev,
                                   uint64_t func,
                                   uint32_t arg3_mode,
                                   uint64_t *out_ret)
{
    aml_tiny_method m;
    aml_tiny_host h;
    uint64_t ret = 0;
    int rc;
    uint32_t i;
    const uint8_t *exec_body = body;
    uint16_t exec_len = len;

    if (!valid || !body || len == 0u || !guid16)
        return -1;

    if (aml_find_method_body_in_blob(body, len, "_DSM", &exec_body, &exec_len) == 0)
    {
        terminal_print("TCPD _DSM unwrap len:");
        terminal_print_inline_hex32(exec_len);
    }
    else
    {
        exec_body = body;
        exec_len = len;
    }

    m.aml = exec_body;
    m.aml_len = exec_len;
    m.scope_prefix = scope_prefix ? scope_prefix : "\\_SB.D0?_";
    m.arg_count = 4u;
    m.use_typed_args = 1u;

    for (i = 0; i < 7u; ++i)
    {
        m.args[i] = 0;
        m.typed_args[i].type = 0u;
        m.typed_args[i].ivalue = 0u;
        m.typed_args[i].name[0] = 0;
        m.typed_args[i].buf_len = 0u;
        m.typed_args[i].pkg_count = 0u;
    }

    /* Arg0 = GUID buffer */
    m.typed_args[0].type = 4u;
    m.typed_args[0].buf_len = 16u;
    for (i = 0; i < 16u; ++i)
        m.typed_args[0].buf[i] = guid16[i];

    /* Arg1 = revision */
    m.typed_args[1].type = 0u;
    m.typed_args[1].ivalue = rev;

    /* Arg2 = function */
    m.typed_args[2].type = 0u;
    m.typed_args[2].ivalue = func;

    /*
      Arg3 variants:
        0 = Package() {}
        1 = Package() { 0 }
        2 = Package() { 1 }
        3 = Integer(0)
        4 = Integer(1)
    */
    switch (arg3_mode)
    {
    default:
    case 0:
        m.typed_args[3].type = 5u;
        m.typed_args[3].pkg_count = 0u;
        break;

    case 1:
        m.typed_args[3].type = 5u;
        m.typed_args[3].pkg_count = 1u;
        m.typed_args[3].pkg_elems[0] = 0u;
        break;

    case 2:
        m.typed_args[3].type = 5u;
        m.typed_args[3].pkg_count = 1u;
        m.typed_args[3].pkg_elems[0] = 1u;
        break;

    case 3:
        m.typed_args[3].type = 0u;
        m.typed_args[3].ivalue = 0u;
        break;

    case 4:
        m.typed_args[3].type = 0u;
        m.typed_args[3].ivalue = 1u;
        break;
    }

    h.read_named_int = tcpd_aml_read_named_int;
    h.write_named_int = tcpd_aml_write_named_int;
    h.log = tcpd_aml_log;
    h.user = 0;

    rc = aml_tiny_exec(&m, &h, &ret);

    terminal_print_inline("DSM ");
    terminal_print_inline(tag);
    terminal_print_inline(" rev:");
    terminal_print_inline_hex32((uint32_t)rev);
    terminal_print_inline(" fn:");
    terminal_print_inline_hex32((uint32_t)func);
    terminal_print_inline(" a3:");
    terminal_print_inline_hex32(arg3_mode);
    terminal_print_inline(" rc:");
    terminal_print_inline_hex32((uint32_t)rc);
    terminal_print_inline(" ret:");
    terminal_print_inline_hex32((uint32_t)ret);

    if (out_ret)
        *out_ret = ret;

    return rc;
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

static void eckb_try_ps0_from_acpi(const hidi2c_acpi_regs *regs)
{
    if (!regs)
        return;
    tcpd_try_method_from_acpi("_PS0",
                              "\\_SB.D0?_",
                              regs->eckb_ps0_body,
                              regs->eckb_ps0_len,
                              aml_body_present(regs->eckb_ps0_body,
                                               regs->eckb_ps0_len,
                                               regs->eckb_ps0_valid));
}

static void eckb_try_on_from_acpi(const hidi2c_acpi_regs *regs)
{
    if (!regs)
        return;
    tcpd_try_method_from_acpi("_ON_",
                              "\\_SB.D0?_",
                              regs->eckb_on_body,
                              regs->eckb_on_len,
                              aml_body_present(regs->eckb_on_body,
                                               regs->eckb_on_len,
                                               regs->eckb_on_valid));
}

static void eckb_try_rst_from_acpi(const hidi2c_acpi_regs *regs)
{
    if (!regs)
        return;
    tcpd_try_method_from_acpi("_RST",
                              "\\_SB.D0?_",
                              regs->eckb_rst_body,
                              regs->eckb_rst_len,
                              aml_body_present(regs->eckb_rst_body,
                                               regs->eckb_rst_len,
                                               regs->eckb_rst_valid));
}

static void eckb_try_sta_from_acpi(const hidi2c_acpi_regs *regs)
{
    if (!regs)
        return;
    tcpd_try_method_from_acpi("_STA",
                              "\\_SB.D0?_",
                              regs->eckb_sta_body,
                              regs->eckb_sta_len,
                              aml_body_present(regs->eckb_sta_body,
                                               regs->eckb_sta_len,
                                               regs->eckb_sta_valid));
}

static void eckb_try_ini_from_acpi(const hidi2c_acpi_regs *regs)
{
    if (!regs)
        return;
    tcpd_try_method_from_acpi("_INI",
                              "\\_SB.D0?_",
                              regs->eckb_ini_body,
                              regs->eckb_ini_len,
                              aml_body_present(regs->eckb_ini_body,
                                               regs->eckb_ini_len,
                                               regs->eckb_ini_valid));
}

static void eckb_try_dsm_from_acpi(const hidi2c_acpi_regs *regs)
{
    static const uint8_t guid_acpi_raw[16] = {
        0xF7, 0xF6, 0xDF, 0x3C,
        0x67, 0x42,
        0x55, 0x45,
        0xAD, 0x05, 0xB3, 0x0A, 0x3D, 0x89, 0x38, 0xDE
    };
    static const uint32_t arg3_modes[] = { 2u, 0u, 1u, 4u, 3u };
    uint64_t ret = 0;

    if (!regs)
        return;

    for (uint32_t i = 0; i < sizeof(arg3_modes) / sizeof(arg3_modes[0]); ++i)
    {
        ret = 0;

        (void)tcpd_run_dsm_typed_guid("ECKB._DSM",
                                      "\\_SB.D0?_",
                                      regs->eckb_dsm_body,
                                      regs->eckb_dsm_len,
                                      aml_body_present(regs->eckb_dsm_body,
                                                       regs->eckb_dsm_len,
                                                       regs->eckb_dsm_valid),
                                      guid_acpi_raw,
                                      1u, 1u, arg3_modes[i], &ret);

        if (!eckb_desc_hint_plausible(ret))
            continue;

        g_kbd.hid_desc_reg = (uint16_t)ret;
        terminal_print("ECKB _DSM hint reg:");
        terminal_print_hex32((uint32_t)g_kbd.hid_desc_reg);
        terminal_print("\n");
        return;
    }
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

static void tcpd_try_gio0_reg_from_acpi(const hidi2c_acpi_regs *regs)
{
    uint64_t args[2];

    if (!regs)
        return;

    /*
      GIO0._REG checks Arg0 against 0x08, then stores Arg1 into GABL.
      Run it once up front to mirror the OpRegion connect/enable path before
      the child _DSM starts advertising a descriptor register hint.
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

static void tcpd_dump_dsm_prefix(const uint8_t *body, uint16_t len, const char *tag)
{
    uint32_t n, i;

    if (!body || len == 0u)
    {
        terminal_print(tag);
        terminal_print_inline(" empty\n");
        return;
    }

    n = (len < 24u) ? len : 24u;

    terminal_print(tag);
    terminal_print_inline(" len:");
    terminal_print_inline_hex32(len);
    terminal_print("bytes:");

    for (i = 0; i < n; ++i)
    {
        terminal_print_inline(" ");
        terminal_print_inline_hex8(body[i]);
    }    
}

static void dump_tcpd_gpio_desc(const hidi2c_acpi_regs *regs)
{
    uint32_t i, n;

    if (!regs || regs->tcpd_gpio_desc_len == 0u)
        return;

    terminal_print("ACPI TCPD gpio desc source:");
    terminal_print_hex8(regs->tcpd_gpio_from_legacy);
    terminal_print(" len:");
    terminal_print_hex32(regs->tcpd_gpio_desc_len);
    terminal_print("\n");

    n = regs->tcpd_gpio_desc_len;
    if (n > 32u)
        n = 32u;

    terminal_print("ACPI TCPD gpio desc raw:");
    for (i = 0; i < n; ++i)
    {
        terminal_print(" ");
        terminal_print_hex8(regs->tcpd_gpio_desc_raw[i]);
    }
    terminal_print("\n");
}

static void tcpd_try_tcpd_dsm_from_acpi(const hidi2c_acpi_regs *regs)
{
    (void)regs;
    terminal_print("TCPD _DSM runtime exec disabled; using child ACPI decode only\n");

    return;

    static const uint8_t guid_acpi_raw[16] = {
        0xF7, 0xF6, 0xDF, 0x3C,
        0x67, 0x42,
        0x55, 0x45,
        0xAD, 0x05, 0xB3, 0x0A, 0x3D, 0x89, 0x38, 0xDE
    };

    uint64_t ret = 0;

    if (!regs)
        return;

    tcpd_dump_dsm_prefix(regs->tcpd_dsm_body,
                         regs->tcpd_dsm_len,
                         "TCPD _DSM prefix");

    /*
      Keep this tiny for now.
      The big brute-force matrix is too expensive and has not produced
      a single useful nonzero return yet.
    */
    (void)tcpd_run_dsm_typed_guid("TCPD._DSM/raw",
                                  "\\_SB.D0?_",
                                  regs->tcpd_dsm_body,
                                  regs->tcpd_dsm_len,
                                  regs->tcpd_dsm_valid,
                                  guid_acpi_raw,
                                  1u, 1u, 2u, &ret);
}

static void tcpd_try_gio0_dsm_from_acpi(const hidi2c_acpi_regs *regs)
{
    static const uint8_t guid_acpi_raw[16] = {
        0xF7, 0xF6, 0xDF, 0x3C,
        0x67, 0x42,
        0x55, 0x45,
        0xAD, 0x05, 0xB3, 0x0A, 0x3D, 0x89, 0x38, 0xDE
    };

    uint64_t ret = 0;

    if (!regs)
        return;

    (void)tcpd_run_dsm_typed_guid("GIO0._DSM",
                                  "\\_SB.GIO0",
                                  regs->tcpd_gio0_dsm_body,
                                  regs->tcpd_gio0_dsm_len,
                                  regs->tcpd_gio0_dsm_valid,
                                  guid_acpi_raw,
                                  1u, 0u, 2u, &ret);

    ret = 0;
    (void)tcpd_run_dsm_typed_guid("GIO0._DSM",
                                  "\\_SB.GIO0",
                                  regs->tcpd_gio0_dsm_body,
                                  regs->tcpd_gio0_dsm_len,
                                  regs->tcpd_gio0_dsm_valid,
                                  guid_acpi_raw,
                                  1u, 1u, 2u, &ret);

    if (tcpd_desc_hint_plausible(ret))
    {
        g_tcpd_desc_reg_hint = (uint16_t)ret;
        terminal_print("TCPD GIO0._DSM hint reg:");
        terminal_print_hex32((uint32_t)g_tcpd_desc_reg_hint);
        terminal_print("\n");
    }
}

static void hidi2c_touchpad_wake_probe(hidi2c_device *dev)
{
    uint8_t reg0[2];
    uint16_t reg;

    if (!dev)
        return;

    reg = g_tcpd_desc_reg_hint ? g_tcpd_desc_reg_hint :
          (dev->hid_desc_reg ? dev->hid_desc_reg : TCPD_DESC_REG_ACPI);

    terminal_print("TCPD: minimal wake probe reg:");
    terminal_print_hex32((uint32_t)reg);
    terminal_print("\n");

    if (g_tpd.gpio_pin != 0u && g_aml_gabl)
    {
        terminal_print("TCPD: pre-probe GPIO wake edge\n");
        tcpd_drive_gpio_wake_edge(1u);
        delay_ms_approx(40u);
    }

    i2c1_bus_set_quiet(1);

    /* 1) address-only nudge */
    (void)i2c1_bus_addr_only(dev->i2c_addr_7bit);
    delay_us_approx(1000u);

    /* 2) write descriptor register pointer */
    wr16(reg0, reg);
    (void)i2c1_bus_write(dev->i2c_addr_7bit, reg0, 2);
    delay_ms_approx(20u);

    i2c1_bus_set_quiet(0);
}

static void hidi2c_keyboard_wake_probe(hidi2c_device *dev)
{
    uint8_t reg0[2];
    uint16_t reg;

    if (!dev)
        return;

    reg = dev->hid_desc_reg ? dev->hid_desc_reg : 0x0001u;

    terminal_print("ECKB: minimal wake probe reg:");
    terminal_print_hex32((uint32_t)reg);
    terminal_print("\n");

    if (g_kbd.gpio_pin != 0u && g_aml_gabl)
    {
        terminal_print("ECKB: pre-probe GPIO wake edge\n");
        tcpd_drive_gpio_wake_edge(1u);
        delay_ms_approx(40u);
    }

    i2c1_bus_set_quiet(1);

    (void)i2c1_bus_addr_only(dev->i2c_addr_7bit);
    delay_us_approx(1000u);

    wr16(reg0, reg);
    (void)i2c1_bus_write(dev->i2c_addr_7bit, reg0, 2);
    delay_ms_approx(20u);

    i2c1_bus_set_quiet(0);
}

/*
  NOTE:
  These GPIO-assisted helpers are intentionally not used in the active TCPD
  bring-up path right now.

  The ACPI GPIO resource pin is not yet proven to be a directly usable TLMM
  GPIO number on this platform, and earlier logs showed that driving it could
  break otherwise-working I2C traffic.
*/

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

static int tcpd_gpio_drive_pair(const hidi2c_acpi_regs *regs,
                                uint32_t first,
                                uint32_t second,
                                uint32_t first_ms,
                                uint32_t second_ms,
                                const char *tag)
{
    int rc;

    if (!regs || !regs->tcpd_gpio_valid)
        return -1;

    terminal_print("TCPD GPIO seq: ");
    terminal_print_inline(tag);
    terminal_print("pin: ");
    terminal_print_inline_hex32(regs->tcpd_gpio_pin);
    

    rc = gpio_init();
    terminal_print("gpio_init rc: ");
    terminal_print_inline_hex32((uint32_t)rc);
    
    if (rc != 0)
        return rc;

    rc = gpio_set_output(regs->tcpd_gpio_pin);
    terminal_print("gpio_set_output rc: ");
    terminal_print_inline_hex32((uint32_t)rc);
    
    if (rc != 0)
        return rc;

    rc = gpio_write(regs->tcpd_gpio_pin, first);
    terminal_print("gpio_write first rc: ");
    terminal_print_inline_hex32((uint32_t)rc);
    
    if (rc != 0)
        return rc;

    delay_ms_approx(first_ms);

    rc = gpio_write(regs->tcpd_gpio_pin, second);
    terminal_print("gpio_write second rc: ");
    terminal_print_inline_hex32((uint32_t)rc);
    
    if (rc != 0)
        return rc;

    delay_ms_approx(second_ms);
    return 0;
}

static int tcpd_gpio_pulse_active_low(const hidi2c_acpi_regs *regs)
{
    return tcpd_gpio_drive_pair(regs,
                                GPIO_VALUE_LOW,
                                GPIO_VALUE_HIGH,
                                20u,
                                120u,
                                "active-low");
}

static int tcpd_gpio_pulse_active_high(const hidi2c_acpi_regs *regs)
{
    return tcpd_gpio_drive_pair(regs,
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

static uint32_t hidi2c_format_report(uint8_t *buf, uint8_t report_id, const uint8_t *data, uint32_t size)
{
    uint32_t length = 2u;

    if (!buf)
        return 0u;

    if (report_id != 0u)
        buf[length++] = report_id;

    if (data && size != 0u)
    {
        for (uint32_t i = 0; i < size; ++i)
            buf[length + i] = data[i];
        length += size;
    }

    wr16(buf, (uint16_t)length);
    return length;
}

static int hidi2c_set_report(hidi2c_device *dev,
                             uint8_t report_type,
                             uint8_t report_id,
                             const uint8_t *payload,
                             uint32_t payload_len)
{
    uint8_t cmd[96];
    uint32_t len = 0u;

    if (!dev || !dev->desc.valid || !payload || payload_len == 0u)
        return -1;

    if ((payload_len + 8u) > sizeof(cmd))
        return -1;

    wr16(cmd + len, dev->desc.wCommandRegister);
    len += 2u;
    len += hidi2c_encode_command(cmd + len, I2C_HID_OPCODE_SET_REPORT, report_type, report_id);
    wr16(cmd + len, dev->desc.wDataRegister);
    len += 2u;
    len += hidi2c_format_report(cmd + len, report_id, payload, payload_len);

    i2c1_bus_set_quiet(1);
    if (i2c1_bus_write(dev->i2c_addr_7bit, cmd, len) != 0)
    {
        i2c1_bus_set_quiet(0);
        return -1;
    }
    i2c1_bus_set_quiet(0);
    return 0;
}

static void tcpd_capture_feature_value(tcpd_feature_value_layout *out,
                                       const uint8_t *has_report_id,
                                       const uint8_t *report_id,
                                       uint32_t report_size,
                                       uint32_t report_count,
                                       uint16_t bit_base,
                                       uint16_t bit_off)
{
    uint8_t rid = 0u;
    uint16_t report_end = 0u;

    if (!out || !has_report_id || !report_id || report_size == 0u || report_count == 0u)
        return;

    rid = *has_report_id ? *report_id : 0u;
    report_end = (uint16_t)(bit_base + (uint16_t)(report_size * report_count));

    if (out->valid)
    {
        uint8_t out_rid = out->has_report_id ? out->report_id : 0u;
        if (out_rid != rid)
            return;
    }
    else
    {
        out->has_report_id = *has_report_id;
        out->report_id = rid;
    }

    if (out->value_size == 0u)
    {
        out->value_bits = bit_off;
        out->value_size = (uint8_t)report_size;
    }

    if (out->report_bits < report_end)
        out->report_bits = report_end;
    out->valid = 1u;
}

static void tcpd_capture_selective_value(tcpd_selective_layout *out,
                                         const uint8_t *has_report_id,
                                         const uint8_t *report_id,
                                         uint32_t report_size,
                                         uint32_t report_count,
                                         uint16_t bit_base,
                                         uint16_t bit_off,
                                         uint32_t usage)
{
    uint8_t rid = 0u;
    uint16_t report_end = 0u;

    if (!out || !has_report_id || !report_id || report_size == 0u || report_count == 0u)
        return;

    rid = *has_report_id ? *report_id : 0u;
    report_end = (uint16_t)(bit_base + (uint16_t)(report_size * report_count));

    if (out->valid || out->surface_size != 0u || out->button_size != 0u)
    {
        uint8_t out_rid = out->has_report_id ? out->report_id : 0u;
        if (out_rid != rid)
            return;
    }
    else
    {
        out->has_report_id = *has_report_id;
        out->report_id = rid;
    }

    if (usage == 0x57u && out->surface_size == 0u)
    {
        out->surface_bits = bit_off;
        out->surface_size = (uint8_t)report_size;
    }
    else if (usage == 0x58u && out->button_size == 0u)
    {
        out->button_bits = bit_off;
        out->button_size = (uint8_t)report_size;
    }

    if (out->report_bits < report_end)
        out->report_bits = report_end;
    if (out->surface_size != 0u || out->button_size != 0u)
        out->valid = 1u;
}

static int tcpd_parse_ptp_feature_reports(const uint8_t *desc, uint16_t len)
{
    struct
    {
        uint8_t usage_page;
        uint32_t report_size;
        uint32_t report_count;
        uint8_t report_id;
        uint8_t has_report_id;
    } g, g_stack[4];
    uint16_t bit_cursor[256];
    uint32_t usages[16];
    uint8_t collection_config[16];
    uint8_t usage_count = 0u;
    uint8_t have_usage_range = 0u;
    uint32_t usage_min = 0u;
    uint32_t usage_max = 0u;
    uint8_t collection_depth = 0u;
    uint8_t stack_depth = 0u;
    uint16_t i = 0u;

    if (!desc)
        return -1;

    tcpd_clear_feature_layouts();
    g.usage_page = 0u;
    g.report_size = 0u;
    g.report_count = 0u;
    g.report_id = 0u;
    g.has_report_id = 0u;

    for (uint32_t k = 0; k < 256u; ++k)
        bit_cursor[k] = 0u;
    for (uint32_t k = 0; k < 16u; ++k)
        collection_config[k] = 0u;

    while (i < len)
    {
        uint8_t b = desc[i++];
        uint8_t size_code;
        uint8_t size;
        uint8_t type;
        uint8_t tag;
        uint32_t val;
        uint8_t current_config = (collection_depth > 0u) ? collection_config[collection_depth - 1u] : 0u;

        if (b == 0xFEu)
        {
            uint8_t long_size;
            if (i + 1u >= len)
                break;
            long_size = desc[i];
            i += 2u;
            if ((uint32_t)i + long_size > len)
                break;
            i = (uint16_t)(i + long_size);
            continue;
        }

        size_code = b & 0x03u;
        size = (size_code == 3u) ? 4u : size_code;
        type = (b >> 2) & 0x03u;
        tag = (b >> 4) & 0x0Fu;

        if ((uint32_t)i + size > len)
            break;

        val = hid_item_u32(&desc[i], size);

        if (type == 0u)
        {
            if (tag == 8u || tag == 9u || tag == 11u)
            {
                if (tag == 11u && current_config && (val & 0x01u) == 0u)
                {
                    uint16_t bit_base = bit_cursor[g.report_id];

                    for (uint32_t field = 0; field < g.report_count; ++field)
                    {
                        uint32_t usage = hid_usage_for_index(usages, usage_count,
                                                             have_usage_range, usage_min, usage_max, field);
                        uint16_t bit_off = (uint16_t)(bit_base + (uint16_t)(field * g.report_size));

                        if (g.usage_page == 0x0Du)
                        {
                            if (usage == 0x52u)
                                tcpd_capture_feature_value(&g_tcpd_input_mode,
                                                           &g.has_report_id,
                                                           &g.report_id,
                                                           g.report_size,
                                                           g.report_count,
                                                           bit_base,
                                                           bit_off);
                            else if (usage == 0x57u || usage == 0x58u)
                                tcpd_capture_selective_value(&g_tcpd_selective,
                                                             &g.has_report_id,
                                                             &g.report_id,
                                                             g.report_size,
                                                             g.report_count,
                                                             bit_base,
                                                             bit_off,
                                                             usage);
                        }
                    }
                }

                bit_cursor[g.report_id] =
                    (uint16_t)(bit_cursor[g.report_id] + (uint16_t)(g.report_size * g.report_count));
            }
            else if (tag == 10u)
            {
                uint32_t usage = 0u;
                uint8_t is_config = current_config;

                if (usage_count > 0u)
                    usage = usages[usage_count - 1u];
                else if (have_usage_range)
                    usage = usage_min;

                if ((val & 0xFFu) == 1u && g.usage_page == 0x0Du && usage == 0x0Eu)
                    is_config = 1u;

                if (collection_depth < 16u)
                    collection_config[collection_depth++] = is_config;
            }
            else if (tag == 12u)
            {
                if (collection_depth > 0u)
                    collection_depth--;
            }

            usage_count = 0u;
            have_usage_range = 0u;
        }
        else if (type == 1u)
        {
            if (tag == 0u)
                g.usage_page = (uint8_t)val;
            else if (tag == 7u)
                g.report_size = val;
            else if (tag == 8u)
            {
                g.report_id = (uint8_t)val;
                g.has_report_id = 1u;
            }
            else if (tag == 9u)
                g.report_count = val;
            else if (tag == 10u)
            {
                if (stack_depth < 4u)
                    g_stack[stack_depth++] = g;
            }
            else if (tag == 11u)
            {
                if (stack_depth > 0u)
                    g = g_stack[--stack_depth];
            }
        }
        else if (type == 2u)
        {
            if (tag == 0u)
            {
                if (usage_count < 16u)
                    usages[usage_count++] = val;
            }
            else if (tag == 1u)
            {
                usage_min = val;
                have_usage_range = 1u;
            }
            else if (tag == 2u)
            {
                usage_max = val;
                have_usage_range = 1u;
            }
        }

        i = (uint16_t)(i + size);
    }

    if (g_tcpd_input_mode.valid || g_tcpd_selective.valid)
        return 0;

    return -1;
}

static int tcpd_send_ptp_feature_report(hidi2c_device *dev,
                                        uint8_t has_report_id,
                                        uint8_t report_id,
                                        uint16_t report_bits,
                                        const tcpd_feature_value_layout *input_mode,
                                        const tcpd_selective_layout *selective)
{
    uint8_t payload[64];
    uint32_t report_bytes = (report_bits + 7u) >> 3;

    if (!dev || report_bits == 0u || report_bytes == 0u || report_bytes > sizeof(payload))
        return -1;

    for (uint32_t i = 0; i < report_bytes; ++i)
        payload[i] = 0u;

    if (input_mode && input_mode->value_size != 0u)
        hid_write_bits(payload, report_bytes,
                       input_mode->value_bits,
                       input_mode->value_size,
                       TCPD_PTP_INPUT_MODE_TOUCHPAD);

    if (selective)
    {
        if (selective->surface_size != 0u)
            hid_write_bits(payload, report_bytes,
                           selective->surface_bits,
                           selective->surface_size,
                           1u);
        if (selective->button_size != 0u)
            hid_write_bits(payload, report_bytes,
                           selective->button_bits,
                           selective->button_size,
                           1u);
    }

    if (hidi2c_set_report(dev,
                          I2C_HID_REPORT_TYPE_FEATURE,
                          has_report_id ? report_id : 0u,
                          payload,
                          report_bytes) != 0)
        return -1;

    delay_ms_approx(20u);
    return 0;
}

static int tcpd_try_enable_ptp_mode(hidi2c_device *dev)
{
    int rc = -1;

    if (!dev || !dev->report_desc_valid || dev->report_desc_len == 0u)
        return -1;

    if (tcpd_parse_ptp_feature_reports(dev->report_desc, dev->report_desc_len) != 0)
    {
        terminal_warn("TCPD PTP feature reports not found");
        return -1;
    }

    terminal_print("TCPD PTP feature input id:");
    terminal_print_hex8(g_tcpd_input_mode.report_id);
    terminal_print(" bits:");
    terminal_print_hex32(g_tcpd_input_mode.report_bits);
    terminal_print(" selective id:");
    terminal_print_hex8(g_tcpd_selective.report_id);
    terminal_print(" bits:");
    terminal_print_hex32(g_tcpd_selective.report_bits);
    terminal_print("\n");

    if (g_tcpd_input_mode.valid &&
        g_tcpd_selective.valid &&
        g_tcpd_input_mode.has_report_id == g_tcpd_selective.has_report_id &&
        g_tcpd_input_mode.report_id == g_tcpd_selective.report_id)
    {
        uint16_t report_bits = g_tcpd_input_mode.report_bits;

        if (report_bits < g_tcpd_selective.report_bits)
            report_bits = g_tcpd_selective.report_bits;

        rc = tcpd_send_ptp_feature_report(dev,
                                          g_tcpd_input_mode.has_report_id,
                                          g_tcpd_input_mode.report_id,
                                          report_bits,
                                          &g_tcpd_input_mode,
                                          &g_tcpd_selective);
    }
    else
    {
        rc = 0;

        if (g_tcpd_input_mode.valid &&
            tcpd_send_ptp_feature_report(dev,
                                         g_tcpd_input_mode.has_report_id,
                                         g_tcpd_input_mode.report_id,
                                         g_tcpd_input_mode.report_bits,
                                         &g_tcpd_input_mode,
                                         0) != 0)
            rc = -1;

        if (rc == 0 &&
            g_tcpd_selective.valid &&
            tcpd_send_ptp_feature_report(dev,
                                         g_tcpd_selective.has_report_id,
                                         g_tcpd_selective.report_id,
                                         g_tcpd_selective.report_bits,
                                         0,
                                         &g_tcpd_selective) != 0)
            rc = -1;
    }

    if (rc == 0)
    {
        terminal_success("TCPD PTP feature mode applied");
        return 0;
    }

    terminal_warn("TCPD PTP feature mode failed");
    return -1;
}

static uint8_t hidi2c_usage_flags(uint32_t usage_page, uint32_t usage)
{
    uint8_t flags = HIDI2C_ACPI_KIND_UNKNOWN;

    if (usage_page == 0x01u)
    {
        if (usage == 0x06u)
            flags |= HIDI2C_ACPI_KIND_KEYBOARD;
        else if (usage == 0x02u)
            flags |= HIDI2C_ACPI_KIND_POINTER;
    }
    else if (usage_page == 0x07u)
    {
        flags |= HIDI2C_ACPI_KIND_KEYBOARD;
    }
    else if (usage_page == 0x0Du)
    {
        if (usage == 0x05u)
            flags |= HIDI2C_ACPI_KIND_POINTER;
    }

    return flags;
}

static uint8_t hidi2c_report_desc_kind(const uint8_t *desc, uint16_t len)
{
    struct
    {
        uint32_t usage_page;
        uint32_t report_size;
        uint32_t report_count;
    } g, g_stack[4];
    uint32_t usages[16];
    uint8_t usage_count = 0u;
    uint8_t have_usage_range = 0u;
    uint32_t usage_min = 0u;
    uint32_t usage_max = 0u;
    uint8_t stack_depth = 0u;
    uint8_t flags = HIDI2C_ACPI_KIND_UNKNOWN;
    uint16_t i = 0u;

    if (!desc || len == 0u)
        return flags;

    g.usage_page = 0u;
    g.report_size = 0u;
    g.report_count = 0u;

    while (i < len)
    {
        uint8_t b = desc[i++];
        uint8_t size_code;
        uint8_t size;
        uint8_t type;
        uint8_t tag;
        uint32_t val;

        if (b == 0xFEu)
        {
            uint8_t long_size;

            if (i + 1u >= len)
                break;

            long_size = desc[i];
            i = (uint16_t)(i + 2u);
            if ((uint32_t)i + long_size > len)
                break;

            i = (uint16_t)(i + long_size);
            continue;
        }

        size_code = b & 0x03u;
        size = (size_code == 3u) ? 4u : size_code;
        type = (b >> 2) & 0x03u;
        tag = (b >> 4) & 0x0Fu;

        if ((uint32_t)i + size > len)
            break;

        val = hid_item_u32(&desc[i], size);

        if (type == 0u)
        {
            if (tag == 8u || tag == 9u || tag == 11u)
            {
                uint32_t count = g.report_count ? g.report_count : 1u;

                if (usage_count == 0u && have_usage_range)
                {
                    flags |= hidi2c_usage_flags(g.usage_page, usage_min);
                    flags |= hidi2c_usage_flags(g.usage_page, usage_max);
                }
                else
                {
                    for (uint32_t u = 0; u < usage_count; ++u)
                    {
                        uint32_t usage = usages[u];
                        uint32_t usage_page = g.usage_page;

                        if (usage > 0xFFFFu)
                        {
                            usage_page = usage >> 16;
                            usage &= 0xFFFFu;
                        }

                        flags |= hidi2c_usage_flags(usage_page, usage);
                    }
                }

                if (g.usage_page == 0x07u && count != 0u)
                    flags |= HIDI2C_ACPI_KIND_KEYBOARD;
            }
            else if (tag == 10u)
            {
                uint32_t usage = 0u;
                uint32_t usage_page = g.usage_page;

                if (usage_count > 0u)
                    usage = usages[usage_count - 1u];
                else if (have_usage_range)
                    usage = usage_min;

                if (usage > 0xFFFFu)
                {
                    usage_page = usage >> 16;
                    usage &= 0xFFFFu;
                }

                flags |= hidi2c_usage_flags(usage_page, usage);
            }

            usage_count = 0u;
            have_usage_range = 0u;
        }
        else if (type == 1u)
        {
            if (tag == 0u)
                g.usage_page = val;
            else if (tag == 7u)
                g.report_size = val;
            else if (tag == 9u)
                g.report_count = val;
            else if (tag == 10u)
            {
                if (stack_depth < 4u)
                    g_stack[stack_depth++] = g;
            }
            else if (tag == 11u)
            {
                if (stack_depth > 0u)
                    g = g_stack[--stack_depth];
            }
        }
        else if (type == 2u)
        {
            if (tag == 0u)
            {
                if (usage_count < 16u)
                    usages[usage_count++] = val;
            }
            else if (tag == 1u)
            {
                usage_min = val;
                have_usage_range = 1u;
            }
            else if (tag == 2u)
            {
                usage_max = val;
                have_usage_range = 1u;
            }
        }

        i = (uint16_t)(i + size);
    }

    return flags;
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
        terminal_print_inline(big_endian ? " ptrwr-BE reg:" : " ptrwr-LE reg:");
        terminal_print_inline_hex32(reg);
        terminal_print_inline(" rc:");
        terminal_print_inline_hex32((uint32_t)rc);
        
        return -1;
    }

    delay_ms_approx(2u);

    rc = i2c1_bus_read(dev->i2c_addr_7bit, rx, sizeof(rx));

    terminal_print(dev->name);
    terminal_print_inline(big_endian ? " rdonly-BE reg:" : " rdonly-LE reg:");
    terminal_print_inline_hex32(reg);
    terminal_print_inline(" rc:");
    terminal_print_inline_hex32((uint32_t)rc);
    terminal_print_inline(" b0:");
    terminal_print_inline_hex8(rx[0]);
    terminal_print_inline(" b1:");
    terminal_print_inline_hex8(rx[1]);
    terminal_print_inline(" b2:");
    terminal_print_inline_hex8(rx[2]);
    terminal_print_inline(" b3:");
    terminal_print_inline_hex8(rx[3]);
    

    if (rc != 0)
        return -1;

    desc_parse(&dev->desc, rx, sizeof(rx));
    if (!desc_looks_valid(&dev->desc))
        return -1;

    dev->hid_desc_reg = reg;
    return 0;
}

static int hidi2c_fetch_desc_touchpad(hidi2c_device *dev)
{
    uint16_t reg;
    uint16_t hint_reg;
    int rc;

    if (!dev)
        return -1;

    reg = dev->hid_desc_reg ? dev->hid_desc_reg : TCPD_DESC_REG_ACPI;
    hint_reg = g_tcpd_desc_reg_hint;

    /*
      First try the lightest path:
      write pointer, then plain read.
    */
    rc = hidi2c_try_desc_reg_read_only_after_pointer(dev, reg, 0);
    if (rc == 0)
        return 0;

    rc = hidi2c_try_desc_reg_read_only_after_pointer(dev, reg, 1);
    if (rc == 0)
        return 0;

    rc = hidi2c_try_desc_reg_read_only_after_pointer(dev, HIDI2C_DESC_REG_FALLBACK, 0);
    if (rc == 0)
        return 0;

    rc = hidi2c_try_desc_reg_read_only_after_pointer(dev, HIDI2C_DESC_REG_FALLBACK, 1);
    if (rc == 0)
        return 0;

    if (hint_reg != 0u &&
        hint_reg != reg &&
        hint_reg != HIDI2C_DESC_REG_FALLBACK)
    {
        terminal_print("TCPD trying hinted reg:");
        terminal_print_hex32((uint32_t)hint_reg);
        terminal_print("\n");

        rc = hidi2c_try_desc_reg_read_only_after_pointer(dev, hint_reg, 0);
        if (rc == 0)
            return 0;

        rc = hidi2c_try_desc_reg_read_only_after_pointer(dev, hint_reg, 1);
        if (rc == 0)
            return 0;
    }

    /*
      Compatibility fallback:
      some devices/controllers behave better with the older split path.
    */
    if (hidi2c_try_desc_reg_split_endian(dev, reg, 0) == 0)
        return 0;
    if (hidi2c_try_desc_reg_split_endian(dev, reg, 1) == 0)
        return 0;

    if (hidi2c_try_desc_reg_split_endian(dev, HIDI2C_DESC_REG_FALLBACK, 0) == 0)
        return 0;
    if (hidi2c_try_desc_reg_split_endian(dev, HIDI2C_DESC_REG_FALLBACK, 1) == 0)
        return 0;

    if (hint_reg != 0u &&
        hint_reg != reg &&
        hint_reg != HIDI2C_DESC_REG_FALLBACK)
    {
        if (hidi2c_try_desc_reg_split_endian(dev, hint_reg, 0) == 0)
            return 0;
        if (hidi2c_try_desc_reg_split_endian(dev, hint_reg, 1) == 0)
            return 0;
    }

    if (hint_reg != 0u)
    {
        terminal_print("TCPD hinted reg no hit\n");
        return -1;
    }

    /*
      Final bounded scan.
      Still runtime-driven, still small, but no longer "0x20 or nothing".
    */
    terminal_print("TCPD scan rdonly start\n");

    for (reg = 0x0018u; reg <= 0x0024u; reg += 2u)
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
            
        }

        short_delay(3000000u);
    }

    return -1;
}

static int tcpd_gpio_hold_and_probe(const hidi2c_acpi_regs *regs,
                                    hidi2c_device *dev,
                                    uint32_t level,
                                    const char *tag)
{
    int rc;

    if (!regs || !dev || !regs->tcpd_gpio_valid)
        return -1;

    terminal_print("TCPD GPIO hold: ");
    terminal_print_inline(tag);
    terminal_print_inline(" pin:");
    terminal_print_inline_hex32(regs->tcpd_gpio_pin);
    terminal_print_inline(" level:");
    terminal_print_inline_hex32(level);

    (void)gpio_init();
    (void)gpio_set_output(regs->tcpd_gpio_pin);
    (void)gpio_write(regs->tcpd_gpio_pin, level);

    delay_ms_approx(20u);

    rc = tcpd_probe_address_linuxish(dev);

    terminal_print("TCPD GPIO hold probe rc:");
    terminal_print_inline_hex32((uint32_t)rc);

    return rc;
}

static int tcpd_probe_after_gpio_only(hidi2c_device *dev, const char *tag)
{
    int rc;

    if (!dev)
        return -1;

    terminal_print("TCPD gpio probe: ");
    terminal_print_inline(tag);

    delay_ms_approx(20u);

    rc = tcpd_probe_address_linuxish(dev);

    terminal_print("TCPD gpio probe rc: ");
    terminal_print_inline_hex32((uint32_t)rc);

    return rc;
}

static int tcpd_try_desc_after_wake(hidi2c_device *dev, const char *tag)
{
    terminal_print("TCPD wake attempt: ");
    terminal_print_inline(tag);

    hidi2c_touchpad_wake_probe(dev);
    delay_ms_approx(120u);

    if (hidi2c_fetch_desc_touchpad_retry(dev, 1u) == 0)
    {
        terminal_print("TCPD wake success: ");
        terminal_print_inline(tag);
        return 0;
    }

    terminal_print("TCPD wake miss: ");
    terminal_print_inline(tag);
    return -1;
}

static void hidi2c_push_unique_reg(uint16_t *tried,
                                   uint32_t *tried_count,
                                   uint32_t tried_cap,
                                   uint16_t reg)
{
    if (!tried || !tried_count || *tried_count >= tried_cap)
        return;

    for (uint32_t i = 0; i < *tried_count; ++i)
    {
        if (tried[i] == reg)
            return;
    }

    tried[(*tried_count)++] = reg;
}

static int hidi2c_try_desc_reg_all_modes(hidi2c_device *dev, uint16_t reg)
{
    if (hidi2c_try_desc_reg_read_only_after_pointer(dev, reg, 0) == 0)
        return 0;
    if (hidi2c_try_desc_reg_read_only_after_pointer(dev, reg, 1) == 0)
        return 0;
    if (hidi2c_try_desc_reg_split_endian(dev, reg, 0) == 0)
        return 0;
    if (hidi2c_try_desc_reg_split_endian(dev, reg, 1) == 0)
        return 0;
    if (hidi2c_try_desc_reg_combined(dev, reg) == 0)
        return 0;

    return -1;
}

static int hidi2c_fetch_desc_generic(hidi2c_device *dev)
{
    static const uint16_t fallback_regs[] = {
        0x0001u, 0x0020u, 0x0010u, 0x0000u,
        0x0018u, 0x001Au, 0x001Cu, 0x001Eu,
        0x0022u, 0x0024u
    };
    uint16_t tried[12];
    uint32_t tried_count = 0;

    if (!dev)
        return -1;

    (void)i2c1_bus_addr_only(dev->i2c_addr_7bit);
    delay_us_approx(500u);

    if (dev->hid_desc_reg != 0u)
        hidi2c_push_unique_reg(tried, &tried_count, sizeof(tried) / sizeof(tried[0]), dev->hid_desc_reg);

    for (uint32_t i = 0; i < (sizeof(fallback_regs) / sizeof(fallback_regs[0])); ++i)
        hidi2c_push_unique_reg(tried, &tried_count, sizeof(tried) / sizeof(tried[0]), fallback_regs[i]);

    terminal_print(dev->name);
    terminal_print(" generic desc probe count:");
    terminal_print_hex32(tried_count);
    terminal_print("\n");

    for (uint32_t i = 0; i < tried_count; ++i)
    {
        uint16_t reg = tried[i];

        terminal_print(dev->name);
        terminal_print(" generic trying reg:");
        terminal_print_hex32(reg);
        terminal_print("\n");

        if (hidi2c_try_desc_reg_all_modes(dev, reg) == 0)
            return 0;
    }

    return -1;
}

static int hidi2c_fetch_desc_keyboard(hidi2c_device *dev)
{
    static const uint16_t fallback_regs[] = {
        0x0020u, 0x0010u, 0x0001u, 0x0000u,
        0x0018u, 0x001Au, 0x001Cu, 0x001Eu,
        0x0022u, 0x0024u
    };
    uint16_t tried[12];
    uint32_t tried_count = 0;

    if (!dev)
        return -1;

    (void)i2c1_bus_addr_only(dev->i2c_addr_7bit);
    delay_us_approx(500u);

    /* Prefer the ACPI hint first, then try a bounded set of common offsets. */
    if (dev->hid_desc_reg != 0u)
        hidi2c_push_unique_reg(tried, &tried_count, sizeof(tried) / sizeof(tried[0]), dev->hid_desc_reg);

    for (uint32_t i = 0; i < (sizeof(fallback_regs) / sizeof(fallback_regs[0])); ++i)
        hidi2c_push_unique_reg(tried, &tried_count, sizeof(tried) / sizeof(tried[0]), fallback_regs[i]);

    terminal_print(dev->name);
    terminal_print(" desc probe count:");
    terminal_print_hex32(tried_count);
    terminal_print("\n");

    for (uint32_t i = 0; i < tried_count; ++i)
    {
        uint16_t reg = tried[i];

        terminal_print(dev->name);
        terminal_print(" trying reg:");
        terminal_print_hex32(reg);
        terminal_print("\n");

        if (hidi2c_try_desc_reg_all_modes(dev, reg) == 0)
            return 0;
    }

    return -1;
}

static int eckb_wake_and_fetch(hidi2c_device *dev, const char *tag, uint32_t settle_ms)
{
    terminal_print("ECKB wake+fetch stage: ");
    terminal_print_inline(tag);

    hidi2c_keyboard_wake_probe(dev);
    delay_ms_approx(settle_ms);

    if (hidi2c_fetch_desc_keyboard(dev) == 0)
    {
        terminal_print("ECKB wake+fetch success: ");
        terminal_print_inline(tag);
        return 0;
    }

    terminal_print("ECKB wake+fetch miss: ");
    terminal_print_inline(tag);
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

static void hidi2c_clear_report_desc(hidi2c_device *dev)
{
    if (!dev)
        return;

    dev->report_desc_len = 0u;
    dev->report_desc_valid = 0u;

    for (uint32_t i = 0; i < sizeof(dev->report_desc); ++i)
        dev->report_desc[i] = 0u;
}

static int hidi2c_read_blob_split(hidi2c_device *dev,
                                  uint16_t reg,
                                  uint8_t *dst,
                                  uint32_t len,
                                  int big_endian)
{
    uint8_t tx[2];
    int rc;

    if (!dev || !dst || len == 0u)
        return -1;

    if (big_endian)
        wr16be(tx, reg);
    else
        wr16(tx, reg);

    i2c1_bus_set_quiet(1);
    rc = i2c1_bus_write_read(dev->i2c_addr_7bit, tx, 2u, dst, len);
    i2c1_bus_set_quiet(0);
    return rc;
}

static int hidi2c_read_blob_combined(hidi2c_device *dev,
                                     uint16_t reg,
                                     uint8_t *dst,
                                     uint32_t len,
                                     int big_endian)
{
    uint8_t tx[2];
    int rc;

    if (!dev || !dst || len == 0u)
        return -1;

    if (big_endian)
        wr16be(tx, reg);
    else
        wr16(tx, reg);

    i2c1_bus_set_quiet(1);
    rc = i2c1_bus_write_read_combined(dev->i2c_addr_7bit, tx, 2u, dst, len);
    i2c1_bus_set_quiet(0);
    return rc;
}

static int hidi2c_read_blob_rdonly_after_pointer(hidi2c_device *dev,
                                                 uint16_t reg,
                                                 uint8_t *dst,
                                                 uint32_t len,
                                                 int big_endian)
{
    uint8_t tx[2];
    int rc;

    if (!dev || !dst || len == 0u)
        return -1;

    if (big_endian)
        wr16be(tx, reg);
    else
        wr16(tx, reg);

    i2c1_bus_set_quiet(1);
    rc = i2c1_bus_write(dev->i2c_addr_7bit, tx, 2u);
    if (rc == 0)
    {
        delay_ms_approx(2u);
        rc = i2c1_bus_read(dev->i2c_addr_7bit, dst, len);
    }
    i2c1_bus_set_quiet(0);
    return rc;
}

static int hidi2c_read_blob_mode(hidi2c_device *dev,
                                 uint16_t reg,
                                 uint8_t *dst,
                                 uint32_t len,
                                 int big_endian,
                                 uint32_t mode)
{
    if (mode == 0u)
        return hidi2c_read_blob_rdonly_after_pointer(dev, reg, dst, len, big_endian);
    if (mode == 1u)
        return hidi2c_read_blob_split(dev, reg, dst, len, big_endian);
    if (mode == 2u)
        return hidi2c_read_blob_combined(dev, reg, dst, len, big_endian);

    return -1;
}

static int hidi2c_try_fetch_report_desc_len_chunked(hidi2c_device *dev,
                                                    uint16_t reg,
                                                    uint32_t want_len,
                                                    uint32_t chunk_len,
                                                    uint32_t mode,
                                                    int big_endian)
{
    uint32_t off = 0u;

    if (!dev || want_len == 0u || want_len > sizeof(dev->report_desc) || chunk_len == 0u)
        return -1;

    for (uint32_t i = 0; i < want_len; ++i)
        dev->report_desc[i] = 0u;

    while (off < want_len)
    {
        uint32_t piece = want_len - off;
        uint16_t piece_reg = 0u;

        if (piece > chunk_len)
            piece = chunk_len;

        if ((uint32_t)reg + off > 0xFFFFu)
            return -1;

        piece_reg = (uint16_t)((uint32_t)reg + off);

        if (hidi2c_read_blob_mode(dev,
                                  piece_reg,
                                  dev->report_desc + off,
                                  piece,
                                  big_endian,
                                  mode) != 0)
            return -1;

        /*
          The descriptor stream is item-heavy, so an all-zero chunk is a good
          signal that this transfer style did not really advance.
        */
        if (!buf_any_nonzero(dev->report_desc + off, piece))
            return -1;

        off += piece;
        if (off < want_len)
            delay_ms_approx(1u);
    }

    return 0;
}

static int hidi2c_try_fetch_report_desc_len(hidi2c_device *dev,
                                            uint16_t reg,
                                            uint32_t want_len)
{
    if (!dev || want_len == 0u || want_len > sizeof(dev->report_desc))
        return -1;

    for (uint32_t i = 0; i < want_len; ++i)
        dev->report_desc[i] = 0u;
    if (hidi2c_read_blob_rdonly_after_pointer(dev, reg, dev->report_desc, want_len, 0) == 0 &&
        buf_any_nonzero(dev->report_desc, want_len))
        return 0;

    for (uint32_t i = 0; i < want_len; ++i)
        dev->report_desc[i] = 0u;
    if (hidi2c_read_blob_split(dev, reg, dev->report_desc, want_len, 0) == 0 &&
        buf_any_nonzero(dev->report_desc, want_len))
        return 0;

    for (uint32_t i = 0; i < want_len; ++i)
        dev->report_desc[i] = 0u;
    if (hidi2c_read_blob_combined(dev, reg, dev->report_desc, want_len, 0) == 0 &&
        buf_any_nonzero(dev->report_desc, want_len))
        return 0;

    for (uint32_t i = 0; i < want_len; ++i)
        dev->report_desc[i] = 0u;
    if (hidi2c_read_blob_rdonly_after_pointer(dev, reg, dev->report_desc, want_len, 1) == 0 &&
        buf_any_nonzero(dev->report_desc, want_len))
        return 0;

    for (uint32_t i = 0; i < want_len; ++i)
        dev->report_desc[i] = 0u;
    if (hidi2c_read_blob_split(dev, reg, dev->report_desc, want_len, 1) == 0 &&
        buf_any_nonzero(dev->report_desc, want_len))
        return 0;

    for (uint32_t i = 0; i < want_len; ++i)
        dev->report_desc[i] = 0u;
    if (hidi2c_read_blob_combined(dev, reg, dev->report_desc, want_len, 1) == 0 &&
        buf_any_nonzero(dev->report_desc, want_len))
        return 0;

    if (want_len > 96u)
    {
        static const uint32_t chunk_sizes[] = {96u, 64u, 32u};

        for (uint32_t chunk_idx = 0; chunk_idx < (sizeof(chunk_sizes) / sizeof(chunk_sizes[0])); ++chunk_idx)
        {
            uint32_t chunk_len = chunk_sizes[chunk_idx];

            if (chunk_len >= want_len)
                continue;

            if (hidi2c_try_fetch_report_desc_len_chunked(dev, reg, want_len, chunk_len, 0u, 0) == 0)
                return 0;
            if (hidi2c_try_fetch_report_desc_len_chunked(dev, reg, want_len, chunk_len, 1u, 0) == 0)
                return 0;
            if (hidi2c_try_fetch_report_desc_len_chunked(dev, reg, want_len, chunk_len, 2u, 0) == 0)
                return 0;
            if (hidi2c_try_fetch_report_desc_len_chunked(dev, reg, want_len, chunk_len, 0u, 1) == 0)
                return 0;
            if (hidi2c_try_fetch_report_desc_len_chunked(dev, reg, want_len, chunk_len, 1u, 1) == 0)
                return 0;
            if (hidi2c_try_fetch_report_desc_len_chunked(dev, reg, want_len, chunk_len, 2u, 1) == 0)
                return 0;
        }
    }

    return -1;
}

static int hidi2c_fetch_report_desc(hidi2c_device *dev)
{
    uint32_t want = 0;
    uint32_t tries[8];
    uint32_t try_count = 0u;

    if (!dev || !dev->desc.valid)
        return -1;

    if (dev->desc.wReportDescLength == 0u || dev->desc.wReportDescRegister == 0u)
        return -1;

    hidi2c_clear_report_desc(dev);

    want = dev->desc.wReportDescLength;
    if (want > sizeof(dev->report_desc))
        want = sizeof(dev->report_desc);

    if (want == 0u)
        return -1;

    tries[try_count++] = want;
    if (want > 256u)
        tries[try_count++] = 256u;
    if (want > 192u)
        tries[try_count++] = 192u;
    if (want > 160u)
        tries[try_count++] = 160u;
    if (want > 128u)
        tries[try_count++] = 128u;
    if (want > 96u)
        tries[try_count++] = 96u;
    if (want > 64u)
        tries[try_count++] = 64u;

    for (uint32_t i = 0; i < try_count; ++i)
    {
        uint32_t fetch_len = tries[i];
        uint8_t duplicate = 0u;

        for (uint32_t j = 0; j < i; ++j)
        {
            if (tries[j] == fetch_len)
            {
                duplicate = 1u;
                break;
            }
        }
        if (duplicate)
            continue;

        if (hidi2c_try_fetch_report_desc_len(dev, dev->desc.wReportDescRegister, fetch_len) == 0)
        {
            dev->report_desc_len = (uint16_t)fetch_len;
            dev->report_desc_valid = 1u;
            terminal_print(dev->name);
            terminal_print(fetch_len < dev->desc.wReportDescLength ? " rptdesc partial len:" : " rptdesc len:");
            terminal_print_hex32(fetch_len);
            terminal_print(" reg:");
            terminal_print_hex32(dev->desc.wReportDescRegister);
            terminal_print("\n");
            return 0;
        }

        hidi2c_clear_report_desc(dev);
    }

    return -1;
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

    dev->last_report.available = 0;
    dev->last_report.len = 0;

    i2c1_bus_set_quiet(1);
    if (i2c1_bus_read(dev->i2c_addr_7bit, rx, want) != 0)
    {
        i2c1_bus_set_quiet(0);
        return -1;
    }
    i2c1_bus_set_quiet(0);

    total = rd16(rx + 0);
    if (total == 0u)
        return 0;
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

    return 0;
}

static int tcpd_try_desc_after_gpio_pulse(const hidi2c_acpi_regs *regs,
                                          hidi2c_device *dev,
                                          int active_low,
                                          const char *tag)
{
    if (!regs || !dev || !regs->tcpd_gpio_valid)
        return -1;

    if (active_low)
        tcpd_gpio_pulse_active_low(regs);
    else
        tcpd_gpio_pulse_active_high(regs);

    delay_ms_approx(40u);

    if (tcpd_probe_after_gpio_only(dev, tag) != 0)
    {
        terminal_print("TCPD gpio pulse probe miss: ");
        terminal_print_inline(tag);
    }

    delay_ms_approx(40u);

    if (hidi2c_fetch_desc_touchpad_retry(dev, 1u) == 0)
    {
        terminal_print("TCPD gpio pulse success: ");
        terminal_print_inline(tag);
        return 0;
    }

    terminal_print("TCPD gpio pulse miss: ");
    terminal_print_inline(tag);
    return -1;
}

static int tcpd_try_desc_after_gpio_hold(const hidi2c_acpi_regs *regs,
                                         hidi2c_device *dev,
                                         uint32_t level,
                                         const char *tag)
{
    if (!regs || !dev || !regs->tcpd_gpio_valid)
        return -1;

    (void)tcpd_gpio_hold_and_probe(regs, dev, level, tag);
    delay_ms_approx(40u);

    if (hidi2c_fetch_desc_touchpad_retry(dev, 1u) == 0)
    {
        terminal_print("TCPD gpio hold success: ");
        terminal_print_inline(tag);
        return 0;
    }

    terminal_print("TCPD gpio hold miss: ");
    terminal_print_inline(tag);
    return -1;
}

static int tcpd_try_fetch_now(hidi2c_device *dev, const char *tag)
{
    terminal_print("TCPD fetch stage: ");
    terminal_print_inline(tag);

    if (hidi2c_fetch_desc_touchpad_retry(dev, 1u) == 0)
    {
        terminal_print("TCPD fetch success: ");
        terminal_print_inline(tag);
        return 0;
    }

    terminal_print("TCPD fetch miss: ");
    terminal_print_inline(tag);
    return -1;
}

static int tcpd_wake_and_fetch(hidi2c_device *dev, const char *tag, uint32_t settle_ms)
{
    terminal_print("TCPD wake+fetch stage: ");
    terminal_print_inline(tag);

    hidi2c_touchpad_wake_probe(dev);
    delay_ms_approx(settle_ms);

    if (hidi2c_fetch_desc_touchpad_retry(dev, 1u) == 0)
    {
        terminal_print("TCPD wake+fetch success: ");
        terminal_print_inline(tag);
        return 0;
    }

    terminal_print("TCPD wake+fetch miss: ");
    terminal_print_inline(tag);
    return -1;
}

static int tcpd_run_acpi_stage_and_probe(const hidi2c_acpi_regs *regs,
                                         hidi2c_device *dev,
                                         const char *tag,
                                         void (*fn)(const hidi2c_acpi_regs *),
                                         uint32_t settle_ms,
                                         int use_wake_probe)
{
    terminal_print("TCPD ACPI stage begin: ");
    terminal_print_inline(tag);

    if (fn)
        fn(regs);

    delay_ms_approx(settle_ms);

    if (use_wake_probe)
        return tcpd_wake_and_fetch(dev, tag, 40u);

    return tcpd_try_fetch_now(dev, tag);
}

static int tcpd_simple_acpi_sequence(const hidi2c_acpi_regs *regs, hidi2c_device *dev)
{
    int ok = 0;

    /*
      The log shows TCPD ACKing reliably while the descriptor reads stay zero.
      Keep the ACPI sequence focused on the stages that were already producing
      useful results in logs, while still connecting the GIO0 opregion once
      before the child _DSM path starts advertising a descriptor hint.
     */

    if (aml_body_present(regs->tcpd_gio0_reg_body,
                         regs->tcpd_gio0_reg_len,
                         regs->tcpd_gio0_reg_valid))
    {
        terminal_print("TCPD ACPI stage begin: GIO0._REG\n");
        tcpd_try_gio0_reg_from_acpi(regs);
        delay_ms_approx(20u);
    }

    if (!ok && tcpd_run_acpi_stage_and_probe(regs, dev, "STA-1", tcpd_try_sta_from_acpi, 20u, 0) == 0)
        ok = 1;

    if (!ok && tcpd_run_acpi_stage_and_probe(regs, dev, "INI-1", tcpd_try_ini_from_acpi, 40u, 1) == 0)
        ok = 1;

    if (!ok)
    {
        terminal_print("TCPD ACPI stage begin: GIO0._DSM-1\n");
        tcpd_try_gio0_dsm_from_acpi(regs);
        delay_ms_approx(80u);

        if (tcpd_wake_and_fetch(dev, "GIO0._DSM-1", 60u) == 0)
            ok = 1;
    }

    if (!ok && tcpd_run_acpi_stage_and_probe(regs, dev, "STA-2", tcpd_try_sta_from_acpi, 20u, 0) == 0)
        ok = 1;

    if (!ok && tcpd_run_acpi_stage_and_probe(regs, dev, "INI-2", tcpd_try_ini_from_acpi, 60u, 1) == 0)
        ok = 1;

    if (!ok)
    {
        terminal_print("TCPD ACPI stage begin: GIO0._DSM-2\n");
        tcpd_try_gio0_dsm_from_acpi(regs);
        delay_ms_approx(120u);

        if (tcpd_wake_and_fetch(dev, "GIO0._DSM-2", 100u) == 0)
            ok = 1;
    }

    return ok ? 0 : -1;
}

static int tcpd_controlled_gpio_test_one_polarity(const hidi2c_acpi_regs *regs,
                                                  hidi2c_device *dev,
                                                  uint32_t active,
                                                  uint32_t inactive,
                                                  const char *tag)
{
    int rc;
    uint32_t level = 0;

    if (!regs || !dev)
        return -1;

    terminal_print("TCPD controlled GPIO test polarity: ");
    terminal_print(tag);
    terminal_print("\n");

    rc = gpio_init();
    terminal_print("TCPD controlled GPIO test gpio_init rc:");
    terminal_print_hex32((uint32_t)rc);
    terminal_print("\n");
    if (rc != 0)
        return -1;

    rc = gpio_set_output(regs->tcpd_gpio_pin);
    terminal_print("TCPD controlled GPIO test gpio_set_output rc:");
    terminal_print_hex32((uint32_t)rc);
    terminal_print("\n");
    if (rc != 0)
        return -1;

    /* start inactive */
    rc = gpio_write(regs->tcpd_gpio_pin, inactive);
    terminal_print("TCPD controlled GPIO write inactive rc:");
    terminal_print_hex32((uint32_t)rc);
    terminal_print("\n");
    if (rc != 0)
        return -1;

    if (gpio_read(regs->tcpd_gpio_pin, &level) == 0)
    {
        terminal_print("TCPD controlled GPIO readback inactive:");
        terminal_print_hex32(level);
        terminal_print("\n");
    }

    delay_ms_approx(10u);

    /* pulse active */
    rc = gpio_write(regs->tcpd_gpio_pin, active);
    terminal_print("TCPD controlled GPIO write active rc:");
    terminal_print_hex32((uint32_t)rc);
    terminal_print("\n");
    if (rc != 0)
        return -1;

    if (gpio_read(regs->tcpd_gpio_pin, &level) == 0)
    {
        terminal_print("TCPD controlled GPIO readback active:");
        terminal_print_hex32(level);
        terminal_print("\n");
    }

    delay_ms_approx(15u);

    /* return inactive */
    rc = gpio_write(regs->tcpd_gpio_pin, inactive);
    terminal_print("TCPD controlled GPIO write inactive2 rc:");
    terminal_print_hex32((uint32_t)rc);
    terminal_print("\n");
    if (rc != 0)
        return -1;

    if (gpio_read(regs->tcpd_gpio_pin, &level) == 0)
    {
        terminal_print("TCPD controlled GPIO readback inactive2:");
        terminal_print_hex32(level);
        terminal_print("\n");
    }

    delay_ms_approx(80u);

    rc = i2c1_bus_addr_only(dev->i2c_addr_7bit);
    terminal_print("TCPD controlled GPIO post-pulse addr rc:");
    terminal_print_hex32((uint32_t)rc);
    terminal_print("\n");

    if (rc != 0)
    {
        terminal_print("TCPD controlled GPIO test: device stopped ACKing after ");
        terminal_print(tag);
        terminal_print("\n");
        return -1;
    }

    delay_ms_approx(40u);

    if (hidi2c_fetch_desc_touchpad_retry(dev, 1u) == 0)
    {
        terminal_print("TCPD controlled GPIO fetch success: ");
        terminal_print_inline(tag);
        return 0;
    }

    terminal_print("TCPD controlled GPIO fetch miss: ");
    terminal_print_inline(tag);
    return -1;
}

static int tcpd_controlled_gpio_test(const hidi2c_acpi_regs *regs, hidi2c_device *dev)
{
    int rc;

    if (!regs || !dev)
        return -1;

    if (regs->tcpd_gpio_pin == 0u)
    {
        terminal_print("TCPD controlled GPIO test: no pin\n");
        return -1;
    }

    if (regs->tcpd_gpio_pin_guessed)
    {
        terminal_print("TCPD controlled GPIO test: skipped because pin is guessed\n");
        return -1;
    }

    terminal_print("TCPD controlled GPIO test pin:");
    terminal_print_hex32(regs->tcpd_gpio_pin);
    terminal_print(" flags:");
    terminal_print_hex32(regs->tcpd_gpio_flags);
    terminal_print(" conn_type:");
    terminal_print_hex8(regs->tcpd_gpio_conn_type);
    terminal_print(" pin_cfg:");
    terminal_print_hex8(regs->tcpd_gpio_pin_cfg);
    terminal_print("\n");

    /*
      Try active-high first because that is what you already tested and it did
      not break ACK. If it still does nothing, try active-low once.
    */
    rc = tcpd_controlled_gpio_test_one_polarity(regs,
                                                dev,
                                                GPIO_VALUE_HIGH,
                                                GPIO_VALUE_LOW,
                                                "gpio-controlled-high");
    if (rc == 0)
        return 0;

    /*
      Only try the second polarity after the first one has failed *without*
      obviously killing the bus. The inner helper already bails if ACK dies.
    */
    return tcpd_controlled_gpio_test_one_polarity(regs,
                                                  dev,
                                                  GPIO_VALUE_LOW,
                                                  GPIO_VALUE_HIGH,
                                                  "gpio-controlled-low");
}

static int hidi2c_probe_acpi_candidate(const hidi2c_acpi_device_candidate *cand,
                                       uint8_t *kind_out)
{
    uint8_t kind = HIDI2C_ACPI_KIND_UNKNOWN;
    uint8_t actual_kind = HIDI2C_ACPI_KIND_UNKNOWN;
    int rc;

    if (kind_out)
        *kind_out = HIDI2C_ACPI_KIND_UNKNOWN;

    if (!cand || cand->addr == 0u)
        return -1;

    hidi2c_init_device_slot(&g_probe_dev,
                            g_probe_name,
                            cand->name,
                            cand->addr,
                            cand->gpio_valid ? cand->gpio_pin : 0u,
                            cand->desc_reg);

    terminal_print("ACPI HID/I2C candidate ");
    terminal_print(g_probe_dev.name);
    terminal_print(" addr:");
    terminal_print_hex8(g_probe_dev.i2c_addr_7bit);
    terminal_print(" reg:");
    terminal_print_hex32(g_probe_dev.hid_desc_reg);
    terminal_print(" hint:");
    terminal_print_hex8(cand->kind_hint);
    terminal_print(" trust:");
    terminal_print_hex8(cand->desc_trusted);
    terminal_print("\n");

    rc = hidi2c_fetch_desc_generic(&g_probe_dev);
    if (rc != 0)
    {
        terminal_warn("ACPI HID/I2C candidate descriptor probe failed");
        return -1;
    }

    if (hidi2c_post_desc_init(&g_probe_dev) != 0)
    {
        terminal_warn("ACPI HID/I2C candidate post-desc init failed");
        return -1;
    }

    if (hidi2c_fetch_report_desc(&g_probe_dev) == 0)
        actual_kind = hidi2c_report_desc_kind(g_probe_dev.report_desc, g_probe_dev.report_desc_len);

    kind = actual_kind;

    terminal_print("ACPI HID/I2C candidate kind actual:");
    terminal_print_hex8(actual_kind);
    terminal_print(" hint:");
    terminal_print_hex8(cand->kind_hint);
    terminal_print(" final:");
    terminal_print_hex8(kind);
    terminal_print("\n");

    if ((kind & (HIDI2C_ACPI_KIND_KEYBOARD | HIDI2C_ACPI_KIND_POINTER)) == 0u)
    {
        terminal_warn("ACPI HID/I2C candidate report descriptor is not keyboard-like or pointer-like");
        return -1;
    }

    g_probe_dev.online = 1u;
    g_probe_dev.last_report.available = 0u;
    g_probe_dev.last_report.len = 0u;

    if (kind_out)
        *kind_out = kind;

    return 0;
}

static uint8_t hidi2c_candidate_already_online(const hidi2c_acpi_device_candidate *cand)
{
    if (!cand)
        return 1u;

    if (g_kbd.online && g_kbd.i2c_addr_7bit == cand->addr)
        return 1u;
    if (g_tpd.online && g_tpd.i2c_addr_7bit == cand->addr)
        return 1u;

    return 0u;
}

static void hidi2c_try_acpi_candidates(const hidi2c_acpi_regs *regs)
{
    if (!regs || regs->device_count == 0u)
        return;

    terminal_print("ACPI HID/I2C generic candidates:");
    terminal_print_hex8(regs->device_count);
    terminal_print("\n");

    for (uint32_t i = 0; i < regs->device_count; ++i)
    {
        const hidi2c_acpi_device_candidate *cand = &regs->devices[i];
        uint8_t kind = HIDI2C_ACPI_KIND_UNKNOWN;

        if (g_kbd.online && g_tpd.online)
            break;

        if (hidi2c_candidate_already_online(cand))
            continue;

        if (hidi2c_probe_acpi_candidate(cand, &kind) != 0)
            continue;

        if ((kind & HIDI2C_ACPI_KIND_KEYBOARD) && !g_kbd.online)
        {
            hidi2c_adopt_device(&g_kbd, g_kbd_name, &g_probe_dev);
            terminal_success("ACPI HID/I2C keyboard-like device online");
            continue;
        }

        if ((kind & HIDI2C_ACPI_KIND_POINTER) && !g_tpd.online)
        {
            hidi2c_adopt_device(&g_tpd, g_tpd_name, &g_probe_dev);
            (void)tcpd_try_enable_ptp_mode(&g_tpd);
            terminal_success("ACPI HID/I2C pointer-like device online");
            continue;
        }
    }
}

void i2c1_hidi2c_init(uint64_t rsdp_phys)
{
    hidi2c_acpi_regs regs;
    int have_regs = 0;

    hidi2c_init_device_slot(&g_kbd, g_kbd_name, "ECKB", ECKB_I2C_ADDR, 0u, 0u);
    hidi2c_init_device_slot(&g_tpd, g_tpd_name, "TCPD", TCPD_I2C_ADDR, 0u, 0u);
    g_tcpd_desc_reg_hint = 0u;

    if (i2c1_bus_init() != 0)
    {
        terminal_warn("i2c1 bus init failed");
        g_bus_ready = 0;
        return;
    }

    g_bus_ready = 1;

    terminal_set_quiet();

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
        if (regs.eckb_gpio_pin != 0u)
            g_kbd.gpio_pin = regs.eckb_gpio_pin;
        if (regs.tcpd_gpio_pin != 0u)
            g_tpd.gpio_pin = regs.tcpd_gpio_pin;

        terminal_print("ACPI ECKB addr:");
        terminal_print_hex8(g_kbd.i2c_addr_7bit);
        terminal_print(" reg:");
        terminal_print_hex32(regs.eckb_desc_reg);
        terminal_print(" trust:");
        terminal_print_hex8(regs.eckb_desc_trusted);
        terminal_print(" gpio:");
        terminal_print_hex32(regs.eckb_gpio_pin);
        terminal_print(" gpio_valid:");
        terminal_print_hex8(regs.eckb_gpio_valid);
        terminal_print(" gpio_flags:");
        terminal_print_hex32(regs.eckb_gpio_flags);
        terminal_print("\n");
        terminal_print("ACPI ECKB methods on:");
        terminal_print_hex8(regs.eckb_on_valid);
        terminal_print(" rst:");
        terminal_print_hex8(regs.eckb_rst_valid);
        terminal_print(" ps0:");
        terminal_print_hex8(regs.eckb_ps0_valid);
        terminal_print(" sta:");
        terminal_print_hex8(regs.eckb_sta_valid);
        terminal_print(" ini:");
        terminal_print_hex8(regs.eckb_ini_valid);
        terminal_print(" dsm:");
        terminal_print_hex8(regs.eckb_dsm_valid);
        terminal_print("\n");
        

        terminal_print("ACPI TCPD addr:");
        terminal_print_hex8(g_tpd.i2c_addr_7bit);
        terminal_print(" reg:");
        terminal_print_hex32(regs.tcpd_desc_reg);
        terminal_print(" trust:");
        terminal_print_hex8(regs.tcpd_desc_trusted);

        terminal_print("ACPI TCPD gpio count:");
        terminal_print_hex8(regs.tcpd_gpio_count);
        terminal_print("\n");

        terminal_print("ACPI TCPD gpio resolved pin:");
        terminal_print_hex32(regs.tcpd_gpio_pin);
        terminal_print(" valid:");
        terminal_print_hex8(regs.tcpd_gpio_valid);
        terminal_print(" flags:");
        terminal_print_hex32(regs.tcpd_gpio_flags);
        terminal_print(" cfg:");
        terminal_print_hex8(regs.tcpd_gpio_pin_cfg);
        terminal_print(" guessed:");
        terminal_print_hex8(regs.tcpd_gpio_pin_guessed);
        terminal_print("\n");

        terminal_print("ACPI TCPD gpio1 pin:");
        terminal_print_hex32(regs.tcpd_gpio_pin1);
        terminal_print(" flags:");
        terminal_print_hex32(regs.tcpd_gpio_flags1);
        terminal_print(" cfg:");
        terminal_print_hex8(regs.tcpd_gpio_pin_cfg1);
        terminal_print(" guessed:");
        terminal_print_hex8(regs.tcpd_gpio_pin_guessed1);
        terminal_print("\n");

        terminal_print("ACPI TCPD gpio2 pin:");
        terminal_print_hex32(regs.tcpd_gpio_pin2);
        terminal_print(" flags:");
        terminal_print_hex32(regs.tcpd_gpio_flags2);
        terminal_print(" cfg:");
        terminal_print_hex8(regs.tcpd_gpio_pin_cfg2);
        terminal_print(" guessed:");
        terminal_print_hex8(regs.tcpd_gpio_pin_guessed2);
        terminal_print("\n");

        dump_tcpd_gpio_desc(&regs);

        hidi2c_try_acpi_candidates(&regs);
    }
    else
    {
        terminal_set_loud();
    }

    if (!g_kbd.online && have_regs && regs.have_eckb)
    {
        if (regs.eckb_desc_reg != 0u)
            g_kbd.hid_desc_reg = regs.eckb_desc_reg;

        g_aml_gabl = 0;
        g_aml_lids = 0;
        g_aml_lidb = 0;
        g_aml_lidr = 1;
        g_aml_t0 = 0;
        g_aml_t1 = 0;
        g_aml_gpio_pin = g_kbd.gpio_pin ? g_kbd.gpio_pin : g_tpd.gpio_pin;

        if (regs.tcpd_gio0_reg_valid)
        {
            terminal_print("ECKB ACPI stage begin: GIO0._REG\n");
            tcpd_try_gio0_reg_from_acpi(&regs);
        }

        if (aml_body_present(regs.eckb_sta_body,
                             regs.eckb_sta_len,
                             regs.eckb_sta_valid))
        {
            terminal_print("ECKB ACPI stage begin: _STA\n");
            eckb_try_sta_from_acpi(&regs);
        }

        if (aml_body_present(regs.eckb_on_body,
                             regs.eckb_on_len,
                             regs.eckb_on_valid))
        {
            terminal_print("ECKB ACPI stage begin: _ON_\n");
            eckb_try_on_from_acpi(&regs);
            delay_ms_approx(20u);
        }

        if (aml_body_present(regs.eckb_ps0_body,
                             regs.eckb_ps0_len,
                             regs.eckb_ps0_valid))
        {
            terminal_print("ECKB ACPI stage begin: _PS0\n");
            eckb_try_ps0_from_acpi(&regs);
            delay_ms_approx(20u);

            if (g_aml_gabl && g_aml_gpio_pin != 0u)
            {
                terminal_print("ECKB ACPI stage: post-_PS0 GPIO wake edge\n");
                tcpd_drive_gpio_wake_edge(1u);
                delay_ms_approx(20u);
            }
        }

        if (aml_body_present(regs.eckb_rst_body,
                             regs.eckb_rst_len,
                             regs.eckb_rst_valid))
        {
            terminal_print("ECKB ACPI stage begin: _RST\n");
            eckb_try_rst_from_acpi(&regs);
            delay_ms_approx(20u);

            if (g_aml_gabl && g_aml_gpio_pin != 0u)
            {
                terminal_print("ECKB ACPI stage: post-_RST GPIO wake edge\n");
                tcpd_drive_gpio_wake_edge(1u);
                delay_ms_approx(20u);
            }
        }

        if (aml_body_present(regs.eckb_ini_body,
                             regs.eckb_ini_len,
                             regs.eckb_ini_valid))
        {
            terminal_print("ECKB ACPI stage begin: _INI\n");
            eckb_try_ini_from_acpi(&regs);
            delay_ms_approx(20u);
        }

        if (aml_body_present(regs.eckb_dsm_body,
                             regs.eckb_dsm_len,
                             regs.eckb_dsm_valid))
        {
            terminal_print("ECKB ACPI stage begin: _DSM(fn=1)\n");
            eckb_try_dsm_from_acpi(&regs);
        }

        if (!regs.eckb_desc_trusted)
            terminal_warn("ECKB ACPI reg untrusted; trying guarded probe");

        {
            int ok = 0;

            if (hidi2c_fetch_desc_keyboard(&g_kbd) == 0)
                ok = 1;

            if (!ok && eckb_wake_and_fetch(&g_kbd, "settle-1", 120u) == 0)
                ok = 1;

            if (!ok && eckb_wake_and_fetch(&g_kbd, "settle-2", 220u) == 0)
                ok = 1;

            if (!ok && eckb_wake_and_fetch(&g_kbd, "settle-3", 320u) == 0)
                ok = 1;

            if (!ok)
            {
                delay_ms_approx(420u);
                if (hidi2c_fetch_desc_keyboard(&g_kbd) == 0)
                    ok = 1;
            }

            if (ok)
            {
                if (hidi2c_post_desc_init(&g_kbd) == 0)
                {
                    (void)hidi2c_fetch_report_desc(&g_kbd);
                    g_kbd.online = 1;
                    terminal_success("ECKB HID-over-I2C online");
                }
                else
                {
                    terminal_warn("ECKB post-desc init failed");
                }
            }
            else
            {
                terminal_warn(regs.eckb_desc_trusted ?
                                  "ECKB trusted ACPI reg failed" :
                                  "ECKB guarded descriptor probe failed");
            }
        }
    }
    else if (!g_kbd.online)
    {
        terminal_warn("ECKB ACPI path missing; skipping");
    }

    if (!g_tpd.online && have_regs && regs.have_tcpd && regs.tcpd_desc_reg != 0u)
    {
        int ok = 0;
        g_aml_gpio_pin = g_tpd.gpio_pin;

        terminal_print("TCPD ACPI quiet init + compat desc probe\n");

        terminal_print("TCPD ACPI desc reg:\n");
        terminal_print_hex32(regs.tcpd_desc_reg);
        terminal_print(" trusted:\n");
        terminal_print_hex8(regs.tcpd_desc_trusted);
        terminal_print(" dsm_valid:\n");
        terminal_print_hex8(regs.tcpd_dsm_valid);
        terminal_print(" dsm_len:\n");
        terminal_print_hex32(regs.tcpd_dsm_len);
        terminal_print(" gio0_dsm_valid:\n");
        terminal_print_hex8(regs.tcpd_gio0_dsm_valid);
        terminal_print(" gio0_dsm_len:\n");
        terminal_print_hex32(regs.tcpd_gio0_dsm_len);
        terminal_print(" gio0_reg_valid:\n");
        terminal_print_hex8(regs.tcpd_gio0_reg_valid);
        terminal_print(" gio0_reg_len:\n");
        terminal_print_hex32(regs.tcpd_gio0_reg_len);
        terminal_print(" ps0_valid:\n");
        terminal_print_hex8(regs.tcpd_ps0_valid);
        terminal_print(" ps0_len:\n");
        terminal_print_hex32(regs.tcpd_ps0_len);

        /*
          Reset AML shadow state before every serious TCPD bring-up attempt.
        */
        g_aml_gabl = 0;
        g_aml_lids = 0;
        g_aml_lidb = 0;
        g_aml_lidr = 1;
        g_aml_t0 = 0;
        g_aml_t1 = 0;

        /*
          Controlled one-shot GPIO test:
          now that the pin decodes consistently as 6 and is not guessed,
          try a single pulse before the ACPI-only sequence.
        */
#if TCPD_GPIO_TEST_ENABLE
        terminal_print("TCPD controlled GPIO test enabled\n");
        if (!ok && tcpd_controlled_gpio_test(&regs, &g_tpd) == 0)
            ok = 1;
#else
        terminal_print("TCPD GPIO-assisted path disabled for safety\n");
#endif

        /*
          First try the simplified ACPI path that only uses the methods that
          are currently behaving usefully in logs.
        */
        if (!ok && tcpd_simple_acpi_sequence(&regs, &g_tpd) == 0)
            ok = 1;

        /*
          If still dead, try repeated no-GPIO wake probes with larger settles.
        */
        if (!ok && g_tcpd_desc_reg_hint != 0u)
        {
            if (tcpd_wake_and_fetch(&g_tpd, "hint-settle", 220u) == 0)
                ok = 1;
        }
        else
        {
            if (!ok && tcpd_wake_and_fetch(&g_tpd, "no-gpio-1", 120u) == 0)
                ok = 1;

            if (!ok && tcpd_wake_and_fetch(&g_tpd, "no-gpio-2", 200u) == 0)
                ok = 1;

            if (!ok && tcpd_wake_and_fetch(&g_tpd, "no-gpio-3", 300u) == 0)
                ok = 1;
        }

        /*
          Final fallback:
          let the descriptor fetch logic do a couple more passes after a long settle.
        */
        if (!ok)
        {
            delay_ms_approx(g_tcpd_desc_reg_hint ? 320u : 250u);
            if (hidi2c_fetch_desc_touchpad_retry(&g_tpd,
                                                g_tcpd_desc_reg_hint ? 1u : 3u) == 0)
                ok = 1;
        }

        if (ok)
        {
            if (hidi2c_post_desc_init(&g_tpd) == 0)
            {
                (void)hidi2c_fetch_report_desc(&g_tpd);
                (void)tcpd_try_enable_ptp_mode(&g_tpd);
                g_tpd.last_report.available = 0u;
                g_tpd.last_report.len = 0u;
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
            terminal_warn("TCPD no valid HID descriptor after ACPI quiet init + compat desc probe");
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
