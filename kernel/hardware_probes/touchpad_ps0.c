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

static int aml_write_cb(void *user, const char *path, uint64_t value)
{
    aml_gpio_ctx *s = (aml_gpio_ctx *)user;

    terminal_print("AML write ");
    terminal_print(path);
    terminal_print(" = ");
    terminal_print_hex64(value);
    terminal_print("\n");

    if (path_eq(path, "\\_SB.GIO0.GABL") || path_eq(path, "\\_SB_.GIO0.GABL"))
    {
        s->gabl = value;
        return 0;
    }

    if (path_eq(path, "\\_SB.LID0.LIDB") || path_eq(path, "\\_SB_.LID0.LIDB"))
    {
        s->lidb = value;
        return 0;
    }

    if (path_eq(path, "\\_SB.GIO0.LIDR") || path_eq(path, "\\_SB_.GIO0.LIDR"))
    {
        s->lidr = value;

        if (s->gabl)
        {
            gpio_set_direction(s->gpio_pin, GPIO_DIR_OUTPUT);
            gpio_write(s->gpio_pin, value ? GPIO_VALUE_HIGH : GPIO_VALUE_LOW);
        }
        return 0;
    }

    return 0;
}

static int aml_read_cb(void *user, const char *path, uint64_t *out)
{
    aml_gpio_ctx *s = (aml_gpio_ctx *)user;

    if (!s || !path || !out)
        return -1;

    if (path_eq(path, "\\_SB.GIO0.GABL") || path_eq(path, "\\_SB_.GIO0.GABL"))
    {
        *out = s->gabl;
        return 0;
    }

    if (path_eq(path, "\\_SB.LID0.LIDB") || path_eq(path, "\\_SB_.LID0.LIDB"))
    {
        *out = s->lidb;
        return 0;
    }

    if (path_eq(path, "\\_SB.GIO0.LIDR") || path_eq(path, "\\_SB_.GIO0.LIDR"))
    {
        *out = s->lidr;
        return 0;
    }

    if (path_eq(path, "\\_SB.LID0.LIDS") || path_eq(path, "\\_SB_.LID0.LIDS"))
    {
        *out = s->lids;
        return 0;
    }

    *out = 0;
    return 0;
}

static int run_one(const uint8_t *body, uint32_t len, aml_gpio_ctx *state)
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
    m.scope_prefix = "\\_SB";

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
    s.lidr = 0;
    s.lidb = 1;
    s.lids = 0;

    terminal_print("PS0: executing...\n");

    if (r.tcpd_gio0_reg_valid && r.tcpd_gio0_reg_len)
        run_one(r.tcpd_gio0_reg_body, r.tcpd_gio0_reg_len, &s);

    run_one(r.tcpd_ps0_body, r.tcpd_ps0_len, &s);

    terminal_print("AML final GABL=");
    terminal_print_hex64(s.gabl);
    terminal_print(" LIDB=");
    terminal_print_hex64(s.lidb);
    terminal_print(" LIDR=");
    terminal_print_hex64(s.lidr);
    terminal_print("\n");

    return 0;
}