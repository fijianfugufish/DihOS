#include "memory/mmio_map.h"
#include "terminal/terminal_api.h"
#include <stdint.h>

#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
#include "memory/aarch64_mmu_map.h"
#define DIHOS_MMIO_BACKEND_AARCH64 1
#elif defined(DIHOS_ARCH_X64) || defined(KERNEL_ARCH_X64) || defined(__x86_64__) || defined(_M_X64)
#include "memory/x64_mmu_map.h"
#define DIHOS_MMIO_BACKEND_X64 1
#endif

void mmio_map_print_state(void)
{
#if defined(DIHOS_MMIO_BACKEND_AARCH64)
    aarch64_mmu_print_state();
#elif defined(DIHOS_MMIO_BACKEND_X64)
    x64_mmu_print_state();
#else
    terminal_print("MMIO map unsupported architecture");
#endif
}

int mmio_map_device_identity(uint64_t phys, uint64_t size)
{
#if defined(DIHOS_MMIO_BACKEND_AARCH64)
    return aarch64_mmu_map_device_identity(phys, size);
#elif defined(DIHOS_MMIO_BACKEND_X64)
    return x64_mmu_map_device_identity(phys, size);
#else
    (void)phys;
    (void)size;
    terminal_print("MMIO map unsupported architecture");
    return -999;
#endif
}

int mmio_map_pci_ecams_from_rsdp(uint64_t rsdp_phys)
{
#if defined(DIHOS_MMIO_BACKEND_AARCH64)
    return aarch64_mmu_map_pci_ecams_from_rsdp(rsdp_phys);
#elif defined(DIHOS_MMIO_BACKEND_X64)
    return x64_mmu_map_pci_ecams_from_rsdp(rsdp_phys);
#else
    (void)rsdp_phys;
    terminal_print("PCI ECAM map unsupported architecture");
    return -999;
#endif
}
