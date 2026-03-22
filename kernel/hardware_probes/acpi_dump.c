#include "hardware_probes/acpi_dump.h"
#include "terminal/terminal_api.h"

#pragma pack(push, 1)

typedef struct
{
    char sig[8]; // "RSD PTR "
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length; // ACPI 2.0+
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} acpi_rsdp_t;

typedef struct
{
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header_t;

#pragma pack(pop)

static int acpi_mem_eq(const char *a, const char *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; ++i)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static uint8_t acpi_checksum_ok(const void *ptr, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)ptr;
    uint32_t i;
    uint8_t sum = 0;

    for (i = 0; i < len; ++i)
        sum = (uint8_t)(sum + p[i]);

    return (sum == 0);
}

static void acpi_copy_sig4(char out[5], const char in[4])
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    out[3] = in[3];
    out[4] = 0;
}

static void acpi_copy_sig8(char out[9], const char in[8])
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    out[3] = in[3];
    out[4] = in[4];
    out[5] = in[5];
    out[6] = in[6];
    out[7] = in[7];
    out[8] = 0;
}

static int acpi_find_bytes(const uint8_t *buf, uint32_t len, const char *pat, uint32_t pat_len)
{
    uint32_t i, j;
    if (!buf || !pat || pat_len == 0 || len < pat_len)
        return 0;

    for (i = 0; i <= len - pat_len; ++i)
    {
        for (j = 0; j < pat_len; ++j)
        {
            if (buf[i + j] != (uint8_t)pat[j])
                break;
        }
        if (j == pat_len)
            return 1;
    }

    return 0;
}

static void acpi_scan_table_strings(const acpi_sdt_header_t *hdr)
{
    const uint8_t *p = (const uint8_t *)hdr;
    uint32_t len = hdr->length;
    char sig[5];

    acpi_copy_sig4(sig, hdr->signature);

    if (acpi_find_bytes(p, len, "PNP0C50", 7))
    {
        terminal_success("ACPI hint: found PNP0C50");
    }

    if (acpi_find_bytes(p, len, "_HID", 4))
    {
        terminal_print("ACPI hint: found _HID");
    }

    if (acpi_find_bytes(p, len, "_CID", 4))
    {
        terminal_print("ACPI hint: found _CID");
    }

    if (acpi_find_bytes(p, len, "I2C", 3))
    {
        terminal_print("ACPI hint: found I2C text");
    }

    if (acpi_find_bytes(p, len, "SPI", 3))
    {
        terminal_print("ACPI hint: found SPI text");
    }

    if (acpi_find_bytes(p, len, "GPIO", 4))
    {
        terminal_print("ACPI hint: found GPIO text");
    }

    if (acpi_find_bytes(p, len, "TPD", 3) || acpi_find_bytes(p, len, "TOUC", 4))
    {
        terminal_print("ACPI hint: possible touchpad/touch text");
    }

    (void)sig;
}

static void acpi_dump_sdt_header(const acpi_sdt_header_t *hdr, const char *prefix)
{
    char sig[5];
    char oem[7];
    char table[9];

    acpi_copy_sig4(sig, hdr->signature);
    oem[0] = hdr->oem_id[0];
    oem[1] = hdr->oem_id[1];
    oem[2] = hdr->oem_id[2];
    oem[3] = hdr->oem_id[3];
    oem[4] = hdr->oem_id[4];
    oem[5] = hdr->oem_id[5];
    oem[6] = 0;

    acpi_copy_sig8(table, hdr->oem_table_id);

    terminal_print(prefix);
    terminal_print("  sig:");
    terminal_print(sig);
    terminal_print("  len:");
    terminal_print_inline_hex32(hdr->length);
    terminal_print("  rev:");
    terminal_print_inline_hex32(hdr->revision);
    terminal_print("  oem:");
    terminal_print(oem);
    terminal_print("  tbl:");
    terminal_print(table);
}

static void acpi_dump_rsdt(const acpi_sdt_header_t *rsdt)
{
    uint32_t entries, i;
    const uint32_t *ptrs;

    if (!rsdt)
        return;

    if (rsdt->length < sizeof(acpi_sdt_header_t))
    {
        terminal_warn("RSDT length too small");
        return;
    }

    entries = (rsdt->length - (uint32_t)sizeof(acpi_sdt_header_t)) / 4u;
    ptrs = (const uint32_t *)((const uint8_t *)rsdt + sizeof(acpi_sdt_header_t));

    terminal_print("ACPI using RSDT");
    terminal_print("RSDT entries:");
    terminal_print_inline_hex32(entries);

    for (i = 0; i < entries; ++i)
    {
        const acpi_sdt_header_t *hdr = (const acpi_sdt_header_t *)(uintptr_t)(uint64_t)ptrs[i];
        if (!hdr)
            continue;

        terminal_print("RSDT entry phys:");
        terminal_print_inline_hex64((uint64_t)(uintptr_t)hdr);

        if (!acpi_mem_eq(hdr->signature, "FACP", 4) &&
            !acpi_mem_eq(hdr->signature, "DSDT", 4) &&
            !acpi_mem_eq(hdr->signature, "SSDT", 4) &&
            !acpi_mem_eq(hdr->signature, "APIC", 4) &&
            !acpi_mem_eq(hdr->signature, "MCFG", 4))
        {
            char sig[5];
            acpi_copy_sig4(sig, hdr->signature);
            terminal_print("table:");
            terminal_print(sig);
        }

        acpi_dump_sdt_header(hdr, "table");
        acpi_scan_table_strings(hdr);

        if (acpi_mem_eq(hdr->signature, "FACP", 4))
        {
            const uint8_t *fadt = (const uint8_t *)hdr;
            uint32_t dsdt32 = 0;
            uint64_t xdsdt = 0;

            if (hdr->length >= 44)
                dsdt32 = *(const uint32_t *)(fadt + 40);
            if (hdr->length >= 148)
                xdsdt = *(const uint64_t *)(fadt + 140);

            terminal_print("FADT DSDT:");
            terminal_print_inline_hex32(dsdt32);
            terminal_print("FADT XDSDT:");
            terminal_print_inline_hex64(xdsdt);

            if (xdsdt)
            {
                const acpi_sdt_header_t *dsdt = (const acpi_sdt_header_t *)(uintptr_t)xdsdt;
                terminal_print("Scanning XDSDT");
                acpi_dump_sdt_header(dsdt, "DSDT");
                acpi_scan_table_strings(dsdt);
            }
            else if (dsdt32)
            {
                const acpi_sdt_header_t *dsdt = (const acpi_sdt_header_t *)(uintptr_t)(uint64_t)dsdt32;
                terminal_print("Scanning DSDT");
                acpi_dump_sdt_header(dsdt, "DSDT");
                acpi_scan_table_strings(dsdt);
            }
        }
    }
}

static void acpi_dump_xsdt(const acpi_sdt_header_t *xsdt)
{
    uint32_t entries, i;
    const uint64_t *ptrs;

    if (!xsdt)
        return;

    if (xsdt->length < sizeof(acpi_sdt_header_t))
    {
        terminal_warn("XSDT length too small");
        return;
    }

    entries = (xsdt->length - (uint32_t)sizeof(acpi_sdt_header_t)) / 8u;
    ptrs = (const uint64_t *)((const uint8_t *)xsdt + sizeof(acpi_sdt_header_t));

    terminal_print("ACPI using XSDT");
    terminal_print("XSDT entries:");
    terminal_print_inline_hex32(entries);

    for (i = 0; i < entries; ++i)
    {
        const acpi_sdt_header_t *hdr = (const acpi_sdt_header_t *)(uintptr_t)ptrs[i];
        if (!hdr)
            continue;

        terminal_print("XSDT entry phys:");
        terminal_print_inline_hex64((uint64_t)(uintptr_t)hdr);

        acpi_dump_sdt_header(hdr, "table");
        acpi_scan_table_strings(hdr);

        if (acpi_mem_eq(hdr->signature, "FACP", 4))
        {
            const uint8_t *fadt = (const uint8_t *)hdr;
            uint32_t dsdt32 = 0;
            uint64_t xdsdt = 0;

            if (hdr->length >= 44)
                dsdt32 = *(const uint32_t *)(fadt + 40);
            if (hdr->length >= 148)
                xdsdt = *(const uint64_t *)(fadt + 140);

            terminal_print("FADT DSDT:");
            terminal_print_inline_hex32(dsdt32);
            terminal_print("FADT XDSDT:");
            terminal_print_inline_hex64(xdsdt);

            if (xdsdt)
            {
                const acpi_sdt_header_t *dsdt = (const acpi_sdt_header_t *)(uintptr_t)xdsdt;
                terminal_print("Scanning XDSDT");
                acpi_dump_sdt_header(dsdt, "DSDT");
                acpi_scan_table_strings(dsdt);
            }
            else if (dsdt32)
            {
                const acpi_sdt_header_t *dsdt = (const acpi_sdt_header_t *)(uintptr_t)(uint64_t)dsdt32;
                terminal_print("Scanning DSDT");
                acpi_dump_sdt_header(dsdt, "DSDT");
                acpi_scan_table_strings(dsdt);
            }
        }
    }
}

void acpi_dump_all(uint64_t rsdp_phys)
{
    const acpi_rsdp_t *rsdp;
    char sig8[9];

    terminal_print("ACPI dump start");

    if (!rsdp_phys)
    {
        terminal_warn("ACPI: no RSDP physical address");
        terminal_flush_log();
        return;
    }

    rsdp = (const acpi_rsdp_t *)(uintptr_t)rsdp_phys;

    acpi_copy_sig8(sig8, rsdp->sig);

    terminal_print("RSDP phys:");
    terminal_print_inline_hex64(rsdp_phys);
    terminal_print("RSDP sig:");
    terminal_print(sig8);
    terminal_print("RSDP rev:");
    terminal_print_inline_hex32(rsdp->revision);
    terminal_print("RSDP RSDT:");
    terminal_print_inline_hex32(rsdp->rsdt_address);
    terminal_print("RSDP XSDT:");
    terminal_print_inline_hex64(rsdp->xsdt_address);
    terminal_flush_log();

    if (!acpi_mem_eq(rsdp->sig, "RSD PTR ", 8))
    {
        terminal_warn("ACPI: RSDP signature mismatch");
        terminal_flush_log();
        return;
    }

    if (!acpi_checksum_ok(rsdp, 20))
    {
        terminal_warn("ACPI: RSDP v1 checksum bad");
    }
    else
    {
        terminal_success("ACPI: RSDP v1 checksum ok");
    }

    if (rsdp->revision >= 2 && rsdp->length >= sizeof(acpi_rsdp_t))
    {
        if (!acpi_checksum_ok(rsdp, rsdp->length))
            terminal_warn("ACPI: RSDP extended checksum bad");
        else
            terminal_success("ACPI: RSDP extended checksum ok");
    }

    if (rsdp->revision >= 2 && rsdp->xsdt_address)
    {
        const acpi_sdt_header_t *xsdt = (const acpi_sdt_header_t *)(uintptr_t)rsdp->xsdt_address;
        acpi_dump_sdt_header(xsdt, "XSDT");
        if (acpi_checksum_ok(xsdt, xsdt->length))
            terminal_success("ACPI: XSDT checksum ok");
        else
            terminal_warn("ACPI: XSDT checksum bad");
        acpi_dump_xsdt(xsdt);
        terminal_flush_log();
        return;
    }

    if (rsdp->rsdt_address)
    {
        const acpi_sdt_header_t *rsdt = (const acpi_sdt_header_t *)(uintptr_t)(uint64_t)rsdp->rsdt_address;
        acpi_dump_sdt_header(rsdt, "RSDT");
        if (acpi_checksum_ok(rsdt, rsdt->length))
            terminal_success("ACPI: RSDT checksum ok");
        else
            terminal_warn("ACPI: RSDT checksum bad");
        acpi_dump_rsdt(rsdt);
        terminal_flush_log();
        return;
    }

    terminal_warn("ACPI: neither XSDT nor RSDT present");
    terminal_flush_log();
}