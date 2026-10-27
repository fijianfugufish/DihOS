#pragma once
#include "dihscover.h"
extern "C" {
#include "netsurf/plot_style.h"
}
int web_font_init(const sacx_api *api);
void web_font_shutdown(void);
void web_font_preferences(uint32_t sans,uint32_t serif,uint32_t mono,uint32_t minimum);
int web_font_width(const plot_font_style_t*,const char*,uint32_t);
uint32_t web_font_position(const plot_font_style_t*,const char*,uint32_t,int,int*);
void web_font_paint(const plot_font_style_t*,const char*,uint32_t,uint32_t*,uint32_t,
                    int,int,int,int,int,int);
