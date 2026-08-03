#pragma once
#include <stdint.h>
#include <stddef.h>
#include "bootinfo.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct pmem_stats
{
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t free_lowdma_pages;
    uint64_t free_high_pages;
    uint64_t free_executable_pages;
    uint64_t page_size;
} pmem_stats;

void pmem_init(const boot_info *bi);
void *pmem_alloc_pages_lowdma(uint64_t n_pages);
void *pmem_alloc_pages(uint64_t n_pages);
void *pmem_alloc_executable_pages(uint64_t n_pages);
void pmem_free_executable_pages(void *p, uint64_t n_pages);
void pmem_free_pages(void *p, uint64_t n); // naive free (optional)
void pmem_get_stats(pmem_stats *out_stats);

/* Address conversion helpers for page-table/MMIO code. */
void *pmem_phys_to_virt(uint64_t phys);
uint64_t pmem_virt_to_phys(const void *virt);

#ifdef __cplusplus
}
#endif
