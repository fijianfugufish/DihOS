#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../sdk/sacx/apps/dihscover/web_font.h"
static FILE *files[8];
static int open_font(const char *path,uint32_t,uint32_t *out) {
    const char *name=strrchr(path,'/');char local[256];
    snprintf(local,sizeof local,"OS/System/Fonts/Web/%s",name+1);
    for(unsigned i=1;i<8;++i)if(!files[i]) { files[i]=fopen(local,"rb");if(!files[i])return -1;*out=i;return 0; }return -1;
}
static uint64_t size_font(uint32_t id) { auto f=files[id];fseek(f,0,SEEK_END);auto size=ftell(f);rewind(f);return size; }
static int read_font(uint32_t id,void *out,uint32_t n,uint32_t *got) { *got=(uint32_t)fread(out,1,n,files[id]);return 0; }
static int close_font(uint32_t id) { fclose(files[id]);files[id]=nullptr;return 0; }
int main() {
    sacx_api api={};api.file_open=open_font;api.file_size=size_font;api.file_read=read_font;api.file_close=close_font;
    assert(web_font_init(&api)==0);
    plot_font_style_t style={};style.family=PLOT_FONT_FAMILY_SANS_SERIF;style.size=12*PLOT_STYLE_SCALE;style.weight=400;
    int narrow=web_font_width(&style,"iiii",4),wide=web_font_width(&style,"WWWW",4);assert(wide>narrow*2);
    style.family=PLOT_FONT_FAMILY_MONOSPACE;assert(web_font_width(&style,"iiii",4)==web_font_width(&style,"WWWW",4));
    style.family=PLOT_FONT_FAMILY_SERIF;int old=web_font_width(&style,"Fractional sizing",17);
    style.size+=PLOT_STYLE_SCALE/2;assert(web_font_width(&style,"Fractional sizing",17)>old);
    const char *unicode="a\xc3\xa9\xce\xa9 z";int x=0;
    unsigned unicode_bytes=(unsigned)strlen(unicode);
    for(int i=0;i<100;++i){unsigned at=web_font_position(&style,unicode,unicode_bytes,i,&x);assert(at<=unicode_bytes);assert(at==unicode_bytes||((unsigned char)unicode[at]&0xc0)!=0x80);}
    std::vector<uint32_t> pixels(200*80,0xffeeeeee);style.foreground=0;
    web_font_paint(&style,"Baseline text",13,pixels.data(),200,0,40,10,10,100,60);
    unsigned changed=0;for(int y=0;y<80;++y)for(int i=0;i<200;++i)if(pixels[y*200+i]!=0xffeeeeee){assert(i>=10&&i<100&&y>=10&&y<60);++changed;}assert(changed);
    for(unsigned i=0;i<300;++i){char text[64];snprintf(text,sizeof text,"Cache eviction %u",i);style.size=(10+i%50)*PLOT_STYLE_SCALE;web_font_width(&style,text,(uint32_t)strlen(text));web_font_paint(&style,text,(uint32_t)strlen(text),pixels.data(),200,0,40,0,0,200,80);}
    web_font_shutdown();web_font_shutdown();assert(web_font_init(&api)==0);web_font_shutdown();
    puts("PASS: packaged fonts, proportional/mono metrics, fractional size, UTF-8 boundaries, raster clipping, cache eviction and reinitialization");
}
