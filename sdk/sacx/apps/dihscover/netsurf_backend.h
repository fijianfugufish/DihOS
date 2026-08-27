#pragma once

#include "dihscover.h"

typedef struct dihscover_netsurf_callbacks {
    void (*title_changed)(const char *title);
    void (*url_changed)(const char *url);
    void (*status_changed)(const char *status);
    void (*pointer_changed)(uint32_t pointer);
} dihscover_netsurf_callbacks;

int dihscover_netsurf_init(const sacx_api *api, uint32_t parent_object,
                           const dihscover_netsurf_callbacks *callbacks);
void dihscover_netsurf_shutdown(void);
int dihscover_netsurf_resize(uint32_t width, uint32_t height);
int dihscover_netsurf_navigate(const char *url);
int dihscover_netsurf_back(void);
int dihscover_netsurf_forward(void);
int dihscover_netsurf_reload(void);
uint32_t dihscover_netsurf_active_fetches(void);
uint32_t dihscover_netsurf_completed_fetches(void);
uint32_t dihscover_netsurf_failed_fetches(void);
void dihscover_netsurf_pump(uint64_t now_ticks);
uint32_t dihscover_netsurf_page_state(void);
void dihscover_netsurf_mouse(int32_t x, int32_t y, uint8_t buttons, int32_t wheel);
