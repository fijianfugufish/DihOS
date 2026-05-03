#include "pci/pci_dump_after_ecam_mapped.h"
#include "hardware_probes/acpi_probe_pci_lookup.h"
#include "pci/pci_ecam_lookup.h"
#include "terminal/terminal_api.h"
#include <stdint.h>

static uint32_t pci_rd32_phys(uint64_t phys)
{
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)phys;
    return *p;
}

static uint16_t lo16(uint32_t v) { return (uint16_t)(v & 0xFFFFu); }
static uint16_t hi16(uint32_t v) { return (uint16_t)((v >> 16) & 0xFFFFu); }

void pci_dump_first_slot_each_ecam_after_mapping(uint64_t rsdp_phys)
{
    dihos_pci_ecam ecams[DIHOS_PCI_ECAM_MAX];
    uint32_t count = acpi_pci_get_ecams_from_rsdp(rsdp_phys, ecams, DIHOS_PCI_ECAM_MAX);

    terminal_print("PCI mapped smoke test: dev0 fn0 only");

    for (uint32_t i = 0; i < count; ++i)
    {
        uint64_t cfg = pci_ecam_config_phys(&ecams[i], ecams[i].start_bus, 0, 0, 0);
        uint32_t id;
        uint32_t classreg;

        terminal_print("about to read cfg=");
        terminal_print_inline_hex64(cfg);

        id = pci_rd32_phys(cfg + 0x00u);

        terminal_print("seg=");
        terminal_print_inline_hex64(ecams[i].segment);
        terminal_print(" vendor=");
        terminal_print_inline_hex64(lo16(id));
        terminal_print(" device=");
        terminal_print_inline_hex64(hi16(id));

        if (lo16(id) == 0xFFFFu)
            continue;

        classreg = pci_rd32_phys(cfg + 0x08u);
        terminal_print(" class=");
        terminal_print_inline_hex64((classreg >> 24) & 0xFFu);
        terminal_print(" subclass=");
        terminal_print_inline_hex64((classreg >> 16) & 0xFFu);
    }
}
