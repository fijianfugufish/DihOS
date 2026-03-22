#pragma once
#include <stdint.h>
#include <stddef.h>
#include "bootinfo.h"

void pmem_init(const boot_info *bi);
void *pmem_alloc_pages_lowdma(uint64_t n_pages);
void *pmem_alloc_pages(uint64_t n_pages);  // returns physical address as void*
void pmem_free_pages(void *p, uint64_t n); // naive free (optional)
