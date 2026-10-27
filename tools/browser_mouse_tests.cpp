#include <cassert>
#include <cstdint>
#include <cstdio>
#include "netsurf/mouse.h"
static struct {void *browser;}g_gui={(void*)1};
static int32_t g_scroll_x=11,g_scroll_y=17,g_mouse_down_x,g_mouse_down_y;
static uint8_t g_previous_buttons;
static unsigned tracks,clicks,navigations,damages;
static int last_x,last_y;
static void clamp_scroll(int x,int64_t y){g_scroll_x=x;g_scroll_y=(int)y;}
static void damage_all(){++damages;}
static void browser_window_mouse_track(void*,browser_mouse_state state,int x,int y){++tracks;if(state&BROWSER_MOUSE_CLICK_1)++navigations;last_x=x;last_y=y;}
static void browser_window_mouse_click(void*,browser_mouse_state state,int x,int y){++clicks;if(state&BROWSER_MOUSE_CLICK_1)++navigations;last_x=x;last_y=y;}
#include "../sdk/sacx/apps/dihscover/netsurf_mouse.inc"
int main(){
 dihscover_netsurf_mouse(20,30,0,0);assert(tracks==1&&!clicks);
 dihscover_netsurf_mouse(20,30,1,0);assert(tracks==2&&!clicks&&!navigations);
 dihscover_netsurf_mouse(20,30,0,0);assert(tracks==2&&clicks==1&&navigations==1);assert(last_x==31&&last_y==47);
 for(unsigned i=0;i<50;++i){dihscover_netsurf_mouse(20,30,1,0);dihscover_netsurf_mouse(20,30,0,0);}
 assert(clicks==51&&navigations==51);
 dihscover_netsurf_mouse(20,30,0,-1);assert(damages==1&&navigations==51&&last_y==95);
 g_gui.browser=nullptr;dihscover_netsurf_mouse(20,30,1,0);dihscover_netsurf_mouse(20,30,0,0);assert(navigations==51);
 puts("PASS: each release activates a link once, press/hover/scroll do not duplicate clicks, and document coordinates include scroll");
}
