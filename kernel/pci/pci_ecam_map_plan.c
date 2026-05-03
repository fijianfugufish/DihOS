#include "pci/pci_ecam_map_plan.h"
#include "terminal/terminal_api.h"
#include <stdint.h>

static uint64_t align_down_2m(uint64_t x)
{
    return x & ~0x1FFFFFULL;
}

static uint64_t align_up_2m(uint64_t x)
{
    return (x + 0x1FFFFFULL) & ~0x1FFFFFULL;
}

uint32_t pci_ecam_get_map_requests_from_rsdp(uint64_t rsdp_phys,
                                             dihos_pci_ecam_map_request *out,
                                             uint32_t max_count)
{
    dihos_pci_ecam ecams[DIHOS_PCI_ECAM_MAX];
    uint32_t count;
    uint32_t out_count = 0;

    if (!out || max_count == 0u)
        return 0;

    count = acpi_pci_get_ecams_from_rsdp(rsdp_phys, ecams, DIHOS_PCI_ECAM_MAX);

    for (uint32_t i = 0; i < count && out_count < max_count; ++i)
    {
        uint64_t buses;
        uint64_t raw_base;
        uint64_t raw_size;
        uint64_t map_base;
        uint64_t map_end;

        if (ecams[i].end_bus < ecams[i].start_bus)
            continue;

        buses = (uint64_t)ecams[i].end_bus - (uint64_t)ecams[i].start_bus + 1ull;
        raw_base = ecams[i].base;
        raw_size = buses << 20; /* 1 MiB per bus in ECAM. */

        map_base = align_down_2m(raw_base);
        map_end = align_up_2m(raw_base + raw_size);

        out[out_count].phys_base = map_base;
        out[out_count].size_bytes = map_end - map_base;
        out[out_count].segment = ecams[i].segment;
        out[out_count].start_bus = ecams[i].start_bus;
        out[out_count].end_bus = ecams[i].end_bus;
        ++out_count;
    }

    return out_count;
}

void pci_ecam_print_map_plan_from_rsdp(uint64_t rsdp_phys)
{
    dihos_pci_ecam_map_request reqs[DIHOS_PCI_ECAM_MAX];
    uint32_t count = pci_ecam_get_map_requests_from_rsdp(rsdp_phys, reqs, DIHOS_PCI_ECAM_MAX);

    terminal_print("PCI ECAM map requests, no config reads yet");
    terminal_print("map request count: ");
    terminal_print_inline_hex64(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        terminal_print("seg=");
        terminal_print_inline_hex64(reqs[i].segment);
        terminal_print(" map phys=");
        terminal_print_inline_hex64(reqs[i].phys_base);
        terminal_print(" size=");
        terminal_print_inline_hex64(reqs[i].size_bytes);
        terminal_print(" buses=");
        terminal_print_inline_hex64(reqs[i].start_bus);
        terminal_print("-");
        terminal_print_inline_hex64(reqs[i].end_bus);
    }
}
