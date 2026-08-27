#pragma once

#include "sacx_api.h"
#include <stdint.h>

#define DIHSCOVER_URL_CAP 2048u

void *dihs_memcpy(void *dst, const void *src, __SIZE_TYPE__ count);
void *dihs_memset(void *dst, int value, __SIZE_TYPE__ count);
uint32_t dihs_strlen(const char *text);
int dihs_streq(const char *a, const char *b);
void dihs_copy(char *dst, uint32_t capacity, const char *src);

int browser_heap_init(void);
void browser_heap_set_api(const sacx_api *api);
void browser_heap_reset(void);
void *browser_heap_alloc(uint32_t size);
void *browser_heap_realloc(void *ptr, uint32_t size);
void browser_heap_free(void *ptr);

