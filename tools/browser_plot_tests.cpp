#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <climits>
#include <cmath>
#include <cstdio>
extern "C" {
#include "utils/errors.h"
#include "netsurf/plotters.h"
#include "netsurf/bitmap.h"
}
struct dihos_bitmap { uint32_t width,height,stride;uint8_t opaque;uint32_t *pixels; };
static uint32_t storage[64*64+2],*g_surface_pixels=storage+1,g_surface_stride=64,g_width=64,g_height=64;
static rect g_clip={0,0,64,64},g_paint_clip={0,0,64,64};static unsigned g_plot_rects,g_plot_bitmaps;
static int maximum(int a,int b){return a>b?a:b;}static int minimum(int a,int b){return a<b?a:b;}
static uint32_t argb(colour c){return 0xff000000|((c&255)<<16)|(c&0xff00)|((c>>16)&255);}
#include "../sdk/sacx/apps/dihscover/netsurf_plotters.inc"
int main(){
 storage[0]=storage[4097]=0x12345678;plot_style_t style={};style.fill_type=PLOT_OP_TYPE_SOLID;style.fill_colour=0xff;style.stroke_type=PLOT_OP_TYPE_SOLID;style.stroke_width=PLOT_STYLE_SCALE;
 rect clip={8,8,48,48};g_paint_clip=clip;plot_clip(nullptr,&clip);
 rect box={10,10,30,30};plot_rectangle(nullptr,&style,&box);
 assert(g_surface_pixels[15*64+15]==0xffff0000);assert(g_surface_pixels[20*64+20]==0xffff0000); /* no diagonal stroke */
 int triangle[]={12,32,32,32,22,46};plot_polygon(nullptr,&style,triangle,3);assert(g_surface_pixels[35*64+22]==0xffff0000);
 float transform[]={1,0,0,1,0,0},path[]={PLOTTER_PATH_MOVE,34,10,PLOTTER_PATH_LINE,44,10,PLOTTER_PATH_LINE,44,20,PLOTTER_PATH_LINE,34,20,PLOTTER_PATH_CLOSE};
 assert(plot_path(nullptr,&style,path,sizeof(path)/sizeof(float),transform)==NSERROR_OK);assert(g_surface_pixels[15*64+38]==0xffff0000);
 float malformed[]={PLOTTER_PATH_BEZIER,1,2};assert(plot_path(nullptr,&style,malformed,3,transform)==NSERROR_BAD_PARAMETER);
 rect huge={INT_MIN,25,INT_MAX,25};plot_line(nullptr,&style,&huge);plot_disc(nullptr,&style,INT_MAX,INT_MAX,INT_MAX);
 uint32_t tile[]={0xff112233,0xff445566};dihos_bitmap bitmap={2,1,2,1,tile};plot_bitmap(nullptr,(struct bitmap*)&bitmap,-1,50,2,1,0,BITMAPF_REPEAT_X|BITMAPF_REPEAT_Y);
 assert(g_surface_pixels[8*64+8]==tile[1]&&g_surface_pixels[8*64+9]==tile[0]);
 for(int y=0;y<64;++y)for(int x=0;x<64;++x)if(x<8||x>=48||y<8||y>=48)assert(!g_surface_pixels[y*64+x]);
 assert(storage[0]==0x12345678&&storage[4097]==0x12345678);
 puts("PASS: rectangle borders, polygon fill, bounded extreme coordinates, nested damage clipping and repeating images");
}
