#include "pci/pci_dump_mapped.h"
#include "hardware_probes/acpi_probe_pci_lookup.h"
#include "pci/pci_ecam_lookup.h"
#include "terminal/terminal_api.h"
#include "asm/asm.h"
#include <stdint.h>

static inline uint32_t mmio_read32(uint64_t addr)
{
    uint32_t v = *(volatile uint32_t *)(uintptr_t)addr;
    asm_mmio_barrier();
    return v;
}

static uint16_t pci_read16(uint64_t cfg, uint16_t off)
{
    uint32_t v = mmio_read32(cfg + (off & ~3u));
    return (uint16_t)((v >> ((off & 2u) * 8u)) & 0xFFFFu);
}

static void pci_dump_one_ecam_bus0(const dihos_pci_ecam *ecam)
{
    uint8_t bus = ecam->start_bus;

    terminal_print("scan seg: ");
    terminal_print_inline_hex64(ecam->segment);
    terminal_print(" bus: ");
    terminal_print_inline_hex64(bus);

    for (uint8_t dev = 0; dev < 32u; ++dev)
    {
        for (uint8_t fn = 0; fn < 8u; ++fn)
        {
            uint64_t cfg = pci_ecam_config_phys(ecam, bus, dev, fn, 0);
            uint16_t vendor;
            uint16_t device;
            uint32_t classreg;
            uint8_t class_code;
            uint8_t subclass;
            uint8_t prog_if;
            uint8_t header_type;

            terminal_print("try cfg: ");
            terminal_print_inline_hex64(cfg);

            vendor = pci_read16(cfg, 0x00);
            if (vendor == 0xFFFFu || vendor == 0x0000u)
            {
                if (fn == 0)
                    break;
                continue;
            }

            device = pci_read16(cfg, 0x02);
            classreg = mmio_read32(cfg + 0x08);
            class_code = (uint8_t)((classreg >> 24) & 0xFFu);
            subclass = (uint8_t)((classreg >> 16) & 0xFFu);
            prog_if = (uint8_t)((classreg >> 8) & 0xFFu);
            header_type = (uint8_t)((mmio_read32(cfg + 0x0C) >> 16) & 0xFFu);

            terminal_print("PCI DEV seg=");
            terminal_print_inline_hex64(ecam->segment);
            terminal_print(" bus=");
            terminal_print_inline_hex64(bus);
            terminal_print(" dev=");
            terminal_print_inline_hex64(dev);
            terminal_print(" fn=");
            terminal_print_inline_hex64(fn);
            terminal_print(" vendor=");
            terminal_print_inline_hex64(vendor);
            terminal_print(" device=");
            terminal_print_inline_hex64(device);
            terminal_print(" class=");
            terminal_print_inline_hex64(class_code);
            terminal_print(" subclass=");
            terminal_print_inline_hex64(subclass);
            terminal_print(" progif=");
            terminal_print_inline_hex64(prog_if);

            if (fn == 0 && (header_type & 0x80u) == 0u)
                break;
        }
    }
}

void pci_dump_mapped_ecam_bus0_from_rsdp(uint64_t rsdp_phys)
{
    dihos_pci_ecam ecams[DIHOS_PCI_ECAM_MAX];
    uint32_t count = acpi_pci_get_ecams_from_rsdp(rsdp_phys, ecams, DIHOS_PCI_ECAM_MAX);

    terminal_print("PCI mapped dump bus0");
    terminal_print("ECAM count: ");
    terminal_print_inline_hex64(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        if (ecams[i].base < 0x0000000100000000ull)
        {
            terminal_print("PCI dump skip: ECAM not mapped in probe mode");
            terminal_print(" seg: ");
            terminal_print_inline_hex64(ecams[i].segment);
            terminal_print(" base: ");
            terminal_print_inline_hex64(ecams[i].base);
            terminal_flush_log();
            continue;
        }

        pci_dump_one_ecam_bus0(&ecams[i]);
        terminal_flush_log();
    }
}

void pci_dump_mapped_ecam_bus0_one_segment_from_rsdp(uint64_t rsdp_phys, uint16_t segment)
{
    dihos_pci_ecam ecams[DIHOS_PCI_ECAM_MAX];
    uint32_t count = acpi_pci_get_ecams_from_rsdp(rsdp_phys, ecams, DIHOS_PCI_ECAM_MAX);

    terminal_print("PCI mapped dump bus0 one segment");
    terminal_print("wanted seg: ");
    terminal_print_inline_hex64(segment);

    for (uint32_t i = 0; i < count; ++i)
    {
        if (ecams[i].segment == segment)
        {
            pci_dump_one_ecam_bus0(&ecams[i]);
            return;
        }
    }

    terminal_print("PCI segment not found");
}
