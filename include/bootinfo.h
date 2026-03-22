#pragma once
#include <stdint.h>

typedef struct
{
    uint64_t fb_base; // physical address
    uint64_t fb_size; // bytes
    uint32_t width, height;
    uint32_t pitch;        // bytes per row
    uint32_t pixel_format; // GOP pixel format enum
    uint32_t rmask, gmask, bmask;
} boot_fb;

typedef struct
{
    uint32_t version; // set to 1
    uint32_t _pad;
    boot_fb fb;
    // (optional) UEFI memory map (raw copy)
    uint64_t mmap; // pointer (in loader’s memory) to memory map copy
    uint64_t mmap_size;
    uint64_t mmap_desc_size;
    uint32_t mmap_desc_version;
    uint32_t _pad2;

    uint64_t kernel_base_phys;  // physical base of loaded kernel image
    uint64_t kernel_size_bytes; // total size (memsz span of PT_LOADs)

    uint64_t acpi_rsdp;      // physical address of ACPI RSDP (from UEFI)
    uint64_t xhci_mmio_base; // 0 if unknown; else kernel will use this

    uint64_t tlmm_mmio_base; /* 0 if unknown */
    uint64_t tlmm_mmio_size; /* 0 if unknown */
} boot_info;
