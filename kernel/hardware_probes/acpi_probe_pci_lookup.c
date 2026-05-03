#include "hardware_probes/acpi_probe_pci_lookup.h"
#include "terminal/terminal_api.h"
#include <stdint.h>
#include <stddef.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

#pragma pack(push, 1)
typedef struct
{
    char sig[8];
    uint8_t csum;
    char oemid[6];
    uint8_t rev;
    uint32_t rsdt;
    uint32_t len;
    uint64_t xsdt;
    uint8_t xsum;
    uint8_t rsv[3];
} acpi_rsdp_t;

typedef struct
{
    char sig[4];
    uint32_t len;
    uint8_t rev;
    uint8_t csum;
    char oemid[6];
    char oemtab[8];
    uint32_t oemrev;
    uint32_t creator;
    uint32_t creatorrev;
} acpi_sdt_t;

typedef struct
{
    acpi_sdt_t h;
    uint64_t entry[1];
} acpi_xsdt_t;

typedef struct
{
    acpi_sdt_t h;
    uint32_t entry[1];
} acpi_rsdt_t;

typedef struct
{
    uint64_t base;
    uint16_t segment;
    uint8_t start_bus;
    uint8_t end_bus;
    uint32_t reserved;
} acpi_mcfg_entry_t;

typedef struct
{
    acpi_sdt_t h;
    uint64_t reserved;
    acpi_mcfg_entry_t entry[1];
} acpi_mcfg_t;
#pragma pack(pop)

static int sig4_eq(const char s[4], const char *lit)
{
    return s[0] == lit[0] && s[1] == lit[1] && s[2] == lit[2] && s[3] == lit[3];
}

static int sane_ptr(uint64_t p)
{
    return p >= 0x1000ull && p < 0x0001000000000000ull;
}

static const acpi_sdt_t *acpi_find_table(uint64_t rsdp_phys, const char *sig)
{
    if (!sane_ptr(rsdp_phys))
        return NULL;

    const acpi_rsdp_t *rsdp = (const acpi_rsdp_t *)(uintptr_t)rsdp_phys;

    if (rsdp->xsdt && sane_ptr(rsdp->xsdt))
    {
        const acpi_xsdt_t *xsdt = (const acpi_xsdt_t *)(uintptr_t)rsdp->xsdt;
        if (!sig4_eq(xsdt->h.sig, "XSDT") || xsdt->h.len < sizeof(acpi_sdt_t))
            return NULL;

        uint32_t n = (xsdt->h.len - sizeof(acpi_sdt_t)) / 8u;
        for (uint32_t i = 0; i < n; ++i)
        {
            uint64_t p = xsdt->entry[i];
            if (!sane_ptr(p))
                continue;
            const acpi_sdt_t *h = (const acpi_sdt_t *)(uintptr_t)p;
            if (sig4_eq(h->sig, sig))
                return h;
        }
    }

    if (rsdp->rsdt && sane_ptr(rsdp->rsdt))
    {
        const acpi_rsdt_t *rsdt = (const acpi_rsdt_t *)(uintptr_t)(uint64_t)rsdp->rsdt;
        if (!sig4_eq(rsdt->h.sig, "RSDT") || rsdt->h.len < sizeof(acpi_sdt_t))
            return NULL;

        uint32_t n = (rsdt->h.len - sizeof(acpi_sdt_t)) / 4u;
        for (uint32_t i = 0; i < n; ++i)
        {
            uint64_t p = (uint64_t)rsdt->entry[i];
            if (!sane_ptr(p))
                continue;
            const acpi_sdt_t *h = (const acpi_sdt_t *)(uintptr_t)p;
            if (sig4_eq(h->sig, sig))
                return h;
        }
    }

    return NULL;
}

uint32_t acpi_pci_get_ecams_from_rsdp(uint64_t rsdp_phys,
                                      dihos_pci_ecam *out,
                                      uint32_t max_count)
{
    if (!out || max_count == 0u)
        return 0;

    const acpi_sdt_t *h = acpi_find_table(rsdp_phys, "MCFG");
    if (!h || h->len < sizeof(acpi_sdt_t) + 8u + sizeof(acpi_mcfg_entry_t))
        return 0;

    const acpi_mcfg_t *mcfg = (const acpi_mcfg_t *)h;
    uint32_t bytes = h->len - (uint32_t)(sizeof(acpi_sdt_t) + 8u);
    uint32_t n = bytes / (uint32_t)sizeof(acpi_mcfg_entry_t);
    uint32_t count = 0;

    for (uint32_t i = 0; i < n && count < max_count; ++i)
    {
        uint64_t base = mcfg->entry[i].base;
        if (!base)
            continue;

        out[count].base = base;
        out[count].segment = mcfg->entry[i].segment;
        out[count].start_bus = mcfg->entry[i].start_bus;
        out[count].end_bus = mcfg->entry[i].end_bus;
        ++count;
    }

    return count;
}

void acpi_pci_print_ecams_from_rsdp(uint64_t rsdp_phys)
{
    dihos_pci_ecam ecams[DIHOS_PCI_ECAM_MAX];
    uint32_t count = acpi_pci_get_ecams_from_rsdp(rsdp_phys, ecams, DIHOS_PCI_ECAM_MAX);

    terminal_print("PCI MCFG ECAM count: ");
    terminal_print_inline_hex64(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        terminal_print("PCI MCFG ECAM base: ");
        terminal_print_inline_hex64(ecams[i].base);
        terminal_print(" seg=");
        terminal_print_inline_hex64(ecams[i].segment);
        terminal_print(" buses=");
        terminal_print_inline_hex64(ecams[i].start_bus);
        terminal_print("-");
        terminal_print_inline_hex64(ecams[i].end_bus);
    }
}
