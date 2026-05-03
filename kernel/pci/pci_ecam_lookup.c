#include "pci/pci_ecam_lookup.h"
#include "terminal/terminal_api.h"
#include <stdint.h>

uint64_t pci_ecam_config_phys(const dihos_pci_ecam *ecam,
                              uint8_t bus,
                              uint8_t dev,
                              uint8_t func,
                              uint16_t offset)
{
    if (!ecam)
        return 0;
    if (bus < ecam->start_bus || bus > ecam->end_bus)
        return 0;
    if (dev >= 32u || func >= 8u || offset >= 4096u)
        return 0;

    /* ECAM: bus 1MiB, device 32KiB, function 4KiB. */
    uint64_t rel_bus = (uint64_t)(bus - ecam->start_bus);
    return ecam->base + (rel_bus << 20) + ((uint64_t)dev << 15) + ((uint64_t)func << 12) + offset;
}

void pci_print_lookup_examples_from_rsdp(uint64_t rsdp_phys)
{
    dihos_pci_ecam ecams[DIHOS_PCI_ECAM_MAX];
    uint32_t count = acpi_pci_get_ecams_from_rsdp(rsdp_phys, ecams, DIHOS_PCI_ECAM_MAX);

    terminal_print("PCI lookup only; no MMIO reads");
    terminal_print("ECAM count: ");
    terminal_print_inline_hex64(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        uint64_t a000 = pci_ecam_config_phys(&ecams[i], ecams[i].start_bus, 0, 0, 0);
        uint64_t a100 = pci_ecam_config_phys(&ecams[i], ecams[i].start_bus, 1, 0, 0);

        terminal_print("seg ");
        terminal_print_inline_hex64(ecams[i].segment);
        terminal_print(" dev0 fn0 cfg: ");
        terminal_print_inline_hex64(a000);
        terminal_print(" dev1 fn0 cfg: ");
        terminal_print_inline_hex64(a100);
    }
}
