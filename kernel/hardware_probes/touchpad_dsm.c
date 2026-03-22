#include "hardware_probes/acpi_probe_hidi2c_ready.h"
#include "hardware_probes/touchpad_dsm.h"
#include "acpi/aml_tiny.h"
#include "terminal/terminal_api.h"
#include "kwrappers/string.h"
#include <stdint.h>

typedef struct aml_dsm_ctx
{
    uint64_t gabl;
    uint64_t ret0;
    uint64_t ret1;
    uint64_t ret2;
} aml_dsm_ctx;

static int contains(const char *s, const char *needle)
{
    return s && needle && strstr(s, needle) != 0;
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
    aml_dsm_ctx *s = (aml_dsm_ctx *)user;

    terminal_print("AML write ");
    terminal_print(path);
    terminal_print(" = ");
    terminal_print_hex64(value);
    terminal_print("\n");

    if (contains(path, "GABL"))
    {
        s->gabl = value;
        return 0;
    }

    if (contains(path, "_T_0"))
    {
        s->ret0 = value;
        return 0;
    }

    if (contains(path, "_T_1"))
    {
        s->ret1 = value;
        return 0;
    }

    if (contains(path, "_T_2"))
    {
        s->ret2 = value;
        return 0;
    }

    return 0;
}

static int aml_read_cb(void *user, const char *path, uint64_t *out)
{
    aml_dsm_ctx *s = (aml_dsm_ctx *)user;

    if (!s || !path || !out)
        return -1;

    terminal_print("AML read ");
    terminal_print(path);
    terminal_print("\n");

    if (contains(path, "GABL"))
    {
        *out = s->gabl;
        return 0;
    }

    if (contains(path, "_T_0"))
    {
        *out = s->ret0;
        return 0;
    }

    if (contains(path, "_T_1"))
    {
        *out = s->ret1;
        return 0;
    }

    if (contains(path, "_T_2"))
    {
        *out = s->ret2;
        return 0;
    }

    *out = 0;
    return 0;
}

static int run_aml(const char *name,
                   const char *scope,
                   const uint8_t *body,
                   uint32_t len,
                   aml_dsm_ctx *state,
                   uint32_t arg_count,
                   uint64_t arg0,
                   uint64_t arg1,
                   uint64_t arg2,
                   uint64_t arg3)
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
    m.args[2] = arg2;
    m.args[3] = arg3;

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

int touchpad_run_dsm(uint64_t rsdp_phys)
{
    hidi2c_acpi_regs r;
    aml_dsm_ctx s;

    terminal_set_quiet();
    if (acpi_hidi2c_get_regs_from_rsdp(rsdp_phys, &r) != 0)
    {
        terminal_set_loud();
        terminal_error("DSM: ACPI parse fail\n");
        return -1;
    }
    terminal_set_loud();

    if (!r.tcpd_dsm_valid || r.tcpd_dsm_len == 0)
    {
        terminal_warn("DSM: TCPD _DSM not present\n");
        return -2;
    }

    s.gabl = 0;
    s.ret0 = 0;
    s.ret1 = 0;
    s.ret2 = 0;

    terminal_print("TCPD DSM wake attempt...\n");

    if (r.tcpd_gio0_reg_valid && r.tcpd_gio0_reg_len)
    {
        run_aml("GIO0._REG",
                "\\_SB.GIO0",
                r.tcpd_gio0_reg_body,
                r.tcpd_gio0_reg_len,
                &s,
                2,
                0x08,
                1,
                0,
                0);

        terminal_print("DSM GABL after _REG: ");
        terminal_print_hex64(s.gabl);
        terminal_print("\n");
    }

    /*
     * First pass: call trusted TCPD _DSM with rev=1, fn=1.
     * Arg0 buffer / Arg3 package are not modelled by aml_tiny yet,
     * so pass 0 placeholders for now and use this to observe method flow.
     */
    run_aml("TCPD._DSM(fn=1)",
            "\\_SB",
            r.tcpd_dsm_body,
            r.tcpd_dsm_len,
            &s,
            4,
            0, /* UUID buffer placeholder */
            1, /* revision */
            1, /* function index */
            0 /* package placeholder */);

    terminal_print("DSM final GABL=");
    terminal_print_hex64(s.gabl);
    terminal_print(" T0=");
    terminal_print_hex64(s.ret0);
    terminal_print(" T1=");
    terminal_print_hex64(s.ret1);
    terminal_print(" T2=");
    terminal_print_hex64(s.ret2);
    terminal_print("\n");

    return 0;
}