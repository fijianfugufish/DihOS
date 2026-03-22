#include "hardware_probes/acpi_probe_hidi2c_ready.h"
#include "acpi/aml_tiny.h"
#include "terminal/terminal_api.h"
#include <stdint.h>

static void aml_log_cb(void *user, const char *msg)
{
    terminal_print("[AML] ");
    terminal_print(msg);
    terminal_print("\n");
}

/* ---- IMPORTANT: intercept GIO0 writes ---- */

static int aml_write_cb(void *user, const char *path, uint64_t value)
{
    terminal_print("[AML WRITE] ");
    terminal_print(path);
    terminal_print(" = ");
    terminal_print_hex64(value);
    terminal_print("\n");

    /* TODO: hook TLMM here later */

    return 0;
}

static int aml_read_cb(void *user, const char *path, uint64_t *out)
{
    /* minimal fake values so AML doesn’t bail */

    if (!path || !out)
        return -1;

    /* Lid open */
    if (!strcmp(path, "\\_SB.LID0.LIDS"))
    {
        *out = 1;
        return 0;
    }

    /* default */
    *out = 0;
    return 0;
}

int touchpad_run_ps0(uint64_t rsdp_phys)
{
    hidi2c_acpi_regs r;

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

    terminal_print("PS0: executing...\n");

    aml_tiny_host host = {0};
    host.log = aml_log_cb;
    host.write_named_int = aml_write_cb;
    host.read_named_int = aml_read_cb;

    aml_tiny_method m = {0};
    m.aml = r.tcpd_ps0_body;
    m.aml_len = r.tcpd_ps0_len;
    m.scope_prefix = "\\_SB"; // important!

    uint64_t ret = 0;

    int rc = aml_tiny_exec(&m, &host, &ret);

    if (rc != 0)
    {
        terminal_error("PS0: exec failed\n");
        return -3;
    }

    terminal_success("PS0: executed\n");
    return 0;
}