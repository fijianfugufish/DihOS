#include "hardware_probes/acpi_probe_hidi2c_ready.h"
#include "acpi/aml_tiny.h"
#include "terminal/terminal_api.h"
#include "gpio/gpio.h"
#include "kwrappers/string.h"
#include <stdint.h>

typedef struct aml_gpio_ctx
{
    uint32_t gpio_pin;
    uint64_t gabl;
    uint64_t lidb;
    uint64_t lidr;
    uint64_t lids;
} aml_gpio_ctx;

static int path_eq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static void aml_log_cb(void *user, const char *msg)
{
    (void)user;
    terminal_print("AML: ");
    terminal_print(msg);
    terminal_print("\n");
}

static void tp_delay(volatile uint32_t n)
{
    while (n--)
        __asm__ volatile("" ::: "memory");
}

static void drive_touchpad_line(aml_gpio_ctx *s, uint64_t value)
{
    if (!s || !s->gabl)
        return;

    gpio_set_direction(s->gpio_pin, GPIO_DIR_OUTPUT);

    /*
     * Current evidence says a steady HIGH is not enough.
     * So when AML asks for 1, generate a rising-edge style wake:
     *   drive low briefly, then high.
     *
     * When AML asks for 0, just drive low.
     */
    if (value)
    {
        gpio_write(s->gpio_pin, GPIO_VALUE_LOW);
        tp_delay(300000);

        gpio_write(s->gpio_pin, GPIO_VALUE_HIGH);
        tp_delay(1500000);

        terminal_print("AML kick GPIO pin ");
        terminal_print_hex32(s->gpio_pin);
        terminal_print(" LOW->HIGH\n");
    }
    else
    {
        gpio_write(s->gpio_pin, GPIO_VALUE_LOW);
        tp_delay(300000);

        terminal_print("AML drive GPIO pin ");
        terminal_print_hex32(s->gpio_pin);
        terminal_print(" <- 0\n");
    }
}

static int aml_write_cb(void *user, const char *path, uint64_t value)
{
    aml_gpio_ctx *s = (aml_gpio_ctx *)user;

    terminal_print("AML write ");
    terminal_print(path);
    terminal_print(" = ");
    terminal_print_hex64(value);
    terminal_print("\n");

    if (strstr(path, "GABL"))
    {
        s->gabl = value;
        return 0;
    }

    if (strstr(path, "LIDR"))
    {
        s->lidr = value;
        drive_touchpad_line(s, value);
        return 0;
    }

    if (strstr(path, "LIDB"))
    {
        s->lidb = value;

        /*
         * In this firmware path _PS0 does:
         *   Store(GIO0.LIDR, LID0.LIDB)
         * So mirror that write to the real line too.
         */
        drive_touchpad_line(s, value);
        return 0;
    }

    if (path_eq(path, "\\_SB.GABL"))
    {
        s->gabl = value;
        return 0;
    }

    return 0;
}

static int aml_read_cb(void *user, const char *path, uint64_t *out)
{
    aml_gpio_ctx *s = (aml_gpio_ctx *)user;

    if (!s || !path || !out)
        return -1;

    terminal_print("AML read ");
    terminal_print(path);
    terminal_print("\n");

    if (strstr(path, "GABL"))
    {
        *out = s->gabl;
        return 0;
    }

    if (strstr(path, "LIDR"))
    {
        *out = s->lidr;
        return 0;
    }

    if (strstr(path, "LIDB"))
    {
        *out = s->lidb;
        return 0;
    }

    if (strstr(path, "LIDS"))
    {
        *out = s->lids;
        return 0;
    }

    if (path_eq(path, "\\_SB.GABL"))
    {
        *out = s->gabl;
        return 0;
    }

    *out = 0;
    return 0;
}

static int run_one(const char *name,
                   const char *scope,
                   const uint8_t *body,
                   uint32_t len,
                   aml_gpio_ctx *state,
                   uint32_t arg_count,
                   uint64_t arg0,
                   uint64_t arg1)
{
    aml_tiny_host host = {0};
    aml_tiny_method m = {0};
    uint64_t ret = 0;
    int rc;

    host.log = aml_log_cb;
    host.read_named_int = aml_read_cb;
    host.write_named_int = aml_write_cb;
    host.user = state;

    m.aml = body;
    m.aml_len = len;
    m.scope_prefix = scope;
    m.arg_count = arg_count;
    m.args[0] = arg0;
    m.args[1] = arg1;

    terminal_print("AML exec ");
    terminal_print(name);
    terminal_print(" len=");
    terminal_print_hex32(len);
    terminal_print("\n");

    rc = aml_tiny_exec(&m, &host, &ret);

    terminal_print("AML exec rc: ");
    terminal_print_hex32((uint32_t)rc);
    terminal_print(" ret: ");
    terminal_print_hex64(ret);
    terminal_print("\n");

    return rc;
}

int touchpad_run_ps0(uint64_t rsdp_phys)
{
    hidi2c_acpi_regs r;
    aml_gpio_ctx s;

    terminal_set_quiet();
    if (acpi_hidi2c_get_regs_from_rsdp(rsdp_phys, &r) != 0)
    {
        terminal_set_loud();
        terminal_error("PS0: ACPI parse fail\n");
        return -1;
    }
    terminal_set_loud();

    if (!r.tcpd_ps0_valid || r.tcpd_ps0_len == 0)
    {
        terminal_warn("PS0: not present\n");
        return -2;
    }

    s.gpio_pin = (uint32_t)r.tcpd_gpio_pin;
    s.gabl = 0;
    s.lidb = 0;
    s.lidr = 1; /* important */
    s.lids = 0; /* make LEqual(LIDS, Zero) true */

    terminal_print("PS0: executing...\n");

    /* _REG(SpaceId=0x08, Connect=1) */
    if (r.tcpd_gio0_reg_valid && r.tcpd_gio0_reg_len)
        run_one("GIO0._REG",
                "\\_SB.GIO0",
                r.tcpd_gio0_reg_body,
                r.tcpd_gio0_reg_len,
                &s,
                2,
                0x08,
                1);

    run_one("_PS0",
            "\\_SB",
            r.tcpd_ps0_body,
            r.tcpd_ps0_len,
            &s,
            0,
            0,
            0);

    if (s.gabl && s.lidb)
    {
        terminal_print("Post-PS0 kick\n");
        drive_touchpad_line(&s, 1);
    }

    terminal_print("AML final GABL=");
    terminal_print_hex64(s.gabl);
    terminal_print(" LIDB=");
    terminal_print_hex64(s.lidb);
    terminal_print(" LIDR=");
    terminal_print_hex64(s.lidr);
    terminal_print("\n");

    return 0;
}