#include "netsurf_backend.h"
#include <stdlib.h>

extern "C" {
#include "netsurf/netsurf.h"
#include "netsurf/content_type.h"
#include "netsurf/content.h"
#include "netsurf/browser_window.h"
#include "netsurf/bitmap.h"
#include "netsurf/fetch.h"
#include "netsurf/layout.h"
#include "netsurf/misc.h"
#include "netsurf/mouse.h"
#include "netsurf/keypress.h"
#include "netsurf/plotters.h"
#include "netsurf/window.h"
#include "content/fetch.h"
#include "content/content.h"
#include "content/hlcache.h"
#include "desktop/browser_history.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
}

struct gui_window { struct browser_window *browser; };
struct dihos_bitmap { uint32_t width,height,stride;uint8_t opaque;uint32_t *pixels; };
struct scheduled_callback { uint64_t due;void (*callback)(void*);void *context;uint8_t used; };

static const sacx_api *g_api;
static dihscover_netsurf_callbacks g_callbacks;
static gui_window g_gui;
static uint32_t g_parent,g_surface_image,g_surface_object,g_caret_object,*g_surface_pixels,g_surface_stride;
static uint32_t g_width=1u,g_height=1u;
static int32_t g_scroll_x,g_scroll_y;
static rect g_clip={0,0,1,1};
static uint8_t g_dirty=1u,g_previous_buttons,g_initialised,g_page_loading;
static int32_t g_mouse_down_x,g_mouse_down_y;
static uint64_t g_now;
static scheduled_callback g_schedule[64];
static uint32_t g_plot_rects,g_plot_texts,g_plot_bitmaps,g_render_reports;

extern "C" const sacx_api *dihscover_netsurf_api(void){return g_api;}
extern "C" void dihscover_netsurf_log(const char *text){if(g_api&&g_api->log&&text)g_api->log(text);}
extern "C" uint32_t dihscover_fetch_active_count(void);
extern "C" uint32_t dihscover_fetch_completed_count(void);
extern "C" uint32_t dihscover_fetch_failed_count(void);

static uint32_t argb(colour value){return 0xff000000u|((value&0xffu)<<16)|(value&0xff00u)|((value>>16)&0xffu);}
static int maximum(int a,int b){return a>b?a:b;}static int minimum(int a,int b){return a<b?a:b;}
static void log_uint(char *out,uint32_t cap,uint32_t *at,uint32_t value){char tmp[12];uint32_t n=0u;if(!value)tmp[n++]='0';while(value&&n<sizeof(tmp)){tmp[n++]=(char)('0'+value%10u);value/=10u;}while(n&&*at+1u<cap)out[(*at)++]=tmp[--n];out[*at]=0;}
static void render_report(uint8_t ok,uint32_t type,uint32_t status)
{
    if(!g_api||!g_api->log)return;char text[192]="Dihscover NetSurf: redraw ";uint32_t at=26u;
    if(ok){text[at++]='o';text[at++]='k';}else{text[at++]='f';text[at++]='a';text[at++]='i';text[at++]='l';text[at++]='e';text[at++]='d';}text[at]=0;
    const char a[]=" type=";for(uint32_t i=0;a[i]&&at+1u<sizeof(text);++i)text[at++]=a[i];log_uint(text,sizeof(text),&at,type);
    const char b[]=" status=";for(uint32_t i=0;b[i]&&at+1u<sizeof(text);++i)text[at++]=b[i];log_uint(text,sizeof(text),&at,status);
    const char c[]=" rect=";for(uint32_t i=0;c[i]&&at+1u<sizeof(text);++i)text[at++]=c[i];log_uint(text,sizeof(text),&at,g_plot_rects);
    const char d[]=" text=";for(uint32_t i=0;d[i]&&at+1u<sizeof(text);++i)text[at++]=d[i];log_uint(text,sizeof(text),&at,g_plot_texts);
    const char e[]=" bitmap=";for(uint32_t i=0;e[i]&&at+1u<sizeof(text);++i)text[at++]=e[i];log_uint(text,sizeof(text),&at,g_plot_bitmaps);g_api->log(text);
}

static void put_pixel(int x,int y,uint32_t value)
{
    if(!g_surface_pixels||x<g_clip.x0||y<g_clip.y0||x>=g_clip.x1||y>=g_clip.y1||x<0||y<0||x>=(int)g_width||y>=(int)g_height)return;
    g_surface_pixels[(uint32_t)y*g_surface_stride+(uint32_t)x]=value;
}

static void fill_rect(int x0,int y0,int x1,int y1,uint32_t value)
{
    x0=maximum(x0,maximum(g_clip.x0,0));y0=maximum(y0,maximum(g_clip.y0,0));x1=minimum(x1,minimum(g_clip.x1,(int)g_width));y1=minimum(y1,minimum(g_clip.y1,(int)g_height));
    for(int y=y0;y<y1;++y){uint32_t *row=g_surface_pixels+(uint32_t)y*g_surface_stride;for(int x=x0;x<x1;++x)row[x]=value;}
}

static nserror plot_clip(const redraw_context *ctx,const rect *clip){(void)ctx;g_clip=*clip;return NSERROR_OK;}
static nserror plot_arc(const redraw_context *ctx,const plot_style_t *style,int x,int y,int radius,int a1,int a2){(void)ctx;(void)style;(void)x;(void)y;(void)radius;(void)a1;(void)a2;return NSERROR_OK;}
static nserror plot_disc(const redraw_context *ctx,const plot_style_t *style,int cx,int cy,int radius)
{
    (void)ctx;if(style->fill_type==PLOT_OP_TYPE_NONE)return NSERROR_OK;uint32_t color=argb(style->fill_colour);
    for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x)if(x*x+y*y<=radius*radius)put_pixel(cx+x,cy+y,color);return NSERROR_OK;
}
static nserror plot_line(const redraw_context *ctx,const plot_style_t *style,const rect *line)
{
    (void)ctx;int x0=line->x0,y0=line->y0,x1=line->x1,y1=line->y1,dx=x1>x0?x1-x0:x0-x1,sx=x0<x1?1:-1,dy=-(y1>y0?y1-y0:y0-y1),sy=y0<y1?1:-1,err=dx+dy;
    uint32_t color=argb(style->stroke_colour);for(;;){put_pixel(x0,y0,color);if(x0==x1&&y0==y1)break;int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}return NSERROR_OK;
}
static nserror plot_rectangle(const redraw_context *ctx,const plot_style_t *style,const rect *box)
{
    ++g_plot_rects;
    if(style->fill_type!=PLOT_OP_TYPE_NONE)fill_rect(box->x0,box->y0,box->x1,box->y1,argb(style->fill_colour));
    if(style->stroke_type!=PLOT_OP_TYPE_NONE){rect edge=*box;plot_line(ctx,style,&edge);edge={box->x1,box->y0,box->x1,box->y1};plot_line(ctx,style,&edge);edge={box->x1,box->y1,box->x0,box->y1};plot_line(ctx,style,&edge);edge={box->x0,box->y1,box->x0,box->y0};plot_line(ctx,style,&edge);}return NSERROR_OK;
}
static nserror plot_polygon(const redraw_context *ctx,const plot_style_t *style,const int *points,unsigned count)
{
    if(count<2u)return NSERROR_OK;plot_style_t line=*style;line.stroke_type=PLOT_OP_TYPE_SOLID;for(unsigned i=0;i<count;++i){rect edge={points[i*2u],points[i*2u+1u],points[((i+1u)%count)*2u],points[((i+1u)%count)*2u+1u]};plot_line(ctx,&line,&edge);}return NSERROR_OK;
}
static nserror plot_path(const redraw_context *ctx,const plot_style_t *style,const float *path,unsigned count,const float transform[6]){(void)ctx;(void)style;(void)path;(void)count;(void)transform;return NSERROR_OK;}

static uint32_t blend(uint32_t dst,uint32_t src)
{
    uint32_t a=src>>24;if(a==255u)return src;if(!a)return dst;uint32_t ia=255u-a;
    uint32_t r=(((src>>16)&255u)*a+((dst>>16)&255u)*ia)/255u,g=(((src>>8)&255u)*a+((dst>>8)&255u)*ia)/255u,b=((src&255u)*a+(dst&255u)*ia)/255u;
    return 0xff000000u|(r<<16)|(g<<8)|b;
}

static nserror plot_bitmap(const redraw_context *ctx,struct bitmap *bitmap,int x,int y,int width,int height,colour background,bitmap_flags_t flags)
{
    ++g_plot_bitmaps;
    (void)ctx;(void)background;(void)flags;dihos_bitmap *source=(dihos_bitmap*)bitmap;if(!source||!source->pixels||width<=0||height<=0)return NSERROR_OK;
    int x0=maximum(x,g_clip.x0),y0=maximum(y,g_clip.y0),x1=minimum(x+width,g_clip.x1),y1=minimum(y+height,g_clip.y1);
    for(int dy=y0;dy<y1;++dy)for(int dx=x0;dx<x1;++dx){uint32_t sx=(uint32_t)((uint64_t)(dx-x)*source->width/(uint32_t)width),sy=(uint32_t)((uint64_t)(dy-y)*source->height/(uint32_t)height);uint32_t *dst=g_surface_pixels+(uint32_t)dy*g_surface_stride+(uint32_t)dx;*dst=blend(*dst,source->pixels[sy*source->stride+sx]);}return NSERROR_OK;
}

static uint32_t text_scale(const plot_font_style_t *style){uint32_t points=(uint32_t)(style->size/PLOT_STYLE_SCALE);uint32_t px=(points*4u+2u)/3u;return px<=20u?1u:px<=36u?2u:3u;}
static nserror plot_text(const redraw_context *ctx,const plot_font_style_t *style,int x,int y,const char *text,size_t length)
{
    ++g_plot_texts;
    (void)ctx;char bounded[1024];uint32_t size=(uint32_t)(length<sizeof(bounded)-1u?length:sizeof(bounded)-1u);dihs_memcpy(bounded,text,size);bounded[size]=0;
    colour c=style->foreground;sacx_color color={(uint8_t)(c&255u),(uint8_t)((c>>8)&255u),(uint8_t)((c>>16)&255u)};
    (void)g_api->img_draw_text(g_surface_image,x,y-(int)(16u*text_scale(style)),bounded,color,255u,text_scale(style));return NSERROR_OK;
}

static plotter_table g_plotters={plot_clip,plot_arc,plot_disc,plot_line,plot_rectangle,plot_polygon,plot_path,plot_bitmap,plot_text,0,0,0,false};

static void *bitmap_create(int width,int height,gui_bitmap_flags flags)
{
    if(width<=0||height<=0||(uint64_t)width*(uint64_t)height>16777216u)return 0;dihos_bitmap *bitmap=(dihos_bitmap*)calloc(1,sizeof(*bitmap));if(!bitmap)return 0;
    bitmap->width=(uint32_t)width;bitmap->height=(uint32_t)height;bitmap->stride=(uint32_t)width;bitmap->opaque=(flags&BITMAP_OPAQUE)?1u:0u;bitmap->pixels=(uint32_t*)malloc((uint32_t)width*(uint32_t)height*4u);
    if(!bitmap->pixels){free(bitmap);return 0;}if(flags&BITMAP_CLEAR)dihs_memset(bitmap->pixels,0,(uint32_t)width*(uint32_t)height*4u);return bitmap;
}
static void bitmap_destroy(void *opaque){dihos_bitmap *bitmap=(dihos_bitmap*)opaque;if(bitmap){free(bitmap->pixels);free(bitmap);}}
static void bitmap_set_opaque(void *opaque,bool value){((dihos_bitmap*)opaque)->opaque=value?1u:0u;}
static bool bitmap_get_opaque(void *opaque){return ((dihos_bitmap*)opaque)->opaque!=0;}
static unsigned char *bitmap_get_buffer(void *opaque){return (unsigned char*)((dihos_bitmap*)opaque)->pixels;}
static size_t bitmap_get_rowstride(void *opaque){return ((dihos_bitmap*)opaque)->stride*4u;}
static int bitmap_get_width(void *opaque){return (int)((dihos_bitmap*)opaque)->width;}
static int bitmap_get_height(void *opaque){return (int)((dihos_bitmap*)opaque)->height;}
static void bitmap_modified(void *opaque){(void)opaque;}
static nserror bitmap_render(struct bitmap *bitmap,hlcache_handle *content){(void)bitmap;(void)content;return NSERROR_OK;}
static gui_bitmap_table g_bitmap_table={bitmap_create,bitmap_destroy,bitmap_set_opaque,bitmap_get_opaque,bitmap_get_buffer,bitmap_get_rowstride,bitmap_get_width,bitmap_get_height,bitmap_modified,bitmap_render};

static nserror font_width(const plot_font_style_t *style,const char *text,size_t length,int *width)
{
    (void)text;
    /* NetSurf asks this question in tight layout loops.  Calling the DihOS
       font service for every candidate substring made long web pages O(n^2)
       and could lock the cooperative kernel indefinitely.  Keep layout
       deliberately monospace here; actual drawing still uses DihOS text. */
    uint32_t unit=8u*text_scale(style);
    if(length>268435455u/unit)*width=0x7fffffff;else *width=(int)(length*unit);
    return NSERROR_OK;
}
static nserror font_position(const plot_font_style_t *style,const char *text,size_t length,int x,size_t *offset,int *actual)
{
    (void)text;uint32_t unit=8u*text_scale(style);size_t fit=x>0?(size_t)((uint32_t)x/unit):0u;
    if(fit>length)fit=length;*offset=fit;*actual=(int)(fit*unit);return NSERROR_OK;
}
static nserror font_split(const plot_font_style_t *style,const char *text,size_t length,int x,size_t *offset,int *actual)
{
    font_position(style,text,length,x,offset,actual);size_t fit=*offset;while(*offset>0&&text[*offset-1]!=' ')(*offset)--;if(!*offset)*offset=fit;if(!*offset&&length)*offset=1;return NSERROR_OK;
}
static gui_layout_table g_layout_table={font_width,font_position,font_split};

static nserror schedule_callback(int delay,void (*callback)(void*),void *context)
{
    for(uint32_t i=0;i<64u;++i)if(g_schedule[i].used&&g_schedule[i].callback==callback&&g_schedule[i].context==context)g_schedule[i].used=0u;
    if(delay<0)return NSERROR_OK;for(uint32_t i=0;i<64u;++i)if(!g_schedule[i].used){g_schedule[i].used=1u;g_schedule[i].due=g_now+(uint32_t)delay;g_schedule[i].callback=callback;g_schedule[i].context=context;if(g_api&&g_api->log&&delay==0)g_api->log("Dihscover NetSurf scheduler: queued immediate callback");return NSERROR_OK;}return NSERROR_NOMEM;
}
static gui_misc_table g_misc_table={schedule_callback,0,0,0,0,0};

static gui_window *window_create(browser_window *browser,gui_window *existing,gui_window_create_flags flags){(void)existing;(void)flags;g_gui.browser=browser;return &g_gui;}
static void window_destroy(gui_window *window){if(window==&g_gui)g_gui.browser=0;}
static nserror window_invalidate(gui_window *window,const rect *area){(void)window;(void)area;g_dirty=1u;return NSERROR_OK;}
static bool window_get_scroll(gui_window *window,int *x,int *y){(void)window;*x=g_scroll_x;*y=g_scroll_y;return true;}
static nserror window_set_scroll(gui_window *window,const rect *area){(void)window;g_scroll_x=area->x0;g_scroll_y=area->y0;g_dirty=1u;return NSERROR_OK;}
static nserror window_get_dimensions(gui_window *window,int *width,int *height){(void)window;*width=(int)g_width;*height=(int)g_height;return NSERROR_OK;}
static nserror window_event(gui_window *window,gui_window_event event){(void)window;if(event==GW_EVENT_START_THROBBER)g_page_loading=1u;else if(event==GW_EVENT_STOP_THROBBER)g_page_loading=0u;g_dirty=1u;return NSERROR_OK;}
static void window_set_title(gui_window *window,const char *title){(void)window;if(g_callbacks.title_changed)g_callbacks.title_changed(title);}
static nserror window_set_url(gui_window *window,nsurl *url){(void)window;if(g_callbacks.url_changed)g_callbacks.url_changed(nsurl_access(url));return NSERROR_OK;}
static void window_set_status(gui_window *window,const char *status){(void)window;if(g_callbacks.status_changed)g_callbacks.status_changed(status);}
static void window_set_pointer(gui_window *window,gui_pointer_shape shape){(void)window;uint32_t pointer=shape==GUI_POINTER_POINT?SACX_MOUSE_CURSOR_LINK:shape==GUI_POINTER_CARET?SACX_MOUSE_CURSOR_BEAM:SACX_MOUSE_CURSOR_ARROW;if(g_callbacks.pointer_changed)g_callbacks.pointer_changed(pointer);}
static void window_place_caret(gui_window *window,int x,int y,int height,const rect *clip)
{
    (void)window;(void)clip;if(!g_api)return;
    if(!g_caret_object){sacx_color c={22,42,60};if(g_api->gfx_obj_add_rect(0,0,2,16,4,c,0,&g_caret_object)!=0)return;(void)g_api->gfx_obj_set_parent(g_caret_object,g_parent);(void)g_api->gfx_obj_set_clip_to_parent(g_caret_object,1u);}
    if(height<4)height=16;(void)g_api->gfx_obj_set_rect(g_caret_object,x,y,2u,(uint32_t)height);(void)g_api->gfx_obj_set_visible(g_caret_object,1u);
}
static gui_window_table g_window_table={window_create,window_destroy,window_invalidate,window_get_scroll,window_set_scroll,window_get_dimensions,window_event,window_set_title,window_set_url,0,window_set_status,window_set_pointer,window_place_caret};

static uint8_t has_css_suffix(const char *path)
{
    uint32_t len=dihs_strlen(path);return len>=4u&&path[len-4u]=='.'&&path[len-3u]=='c'&&path[len-2u]=='s'&&path[len-1u]=='s';
}
static const char *fetch_filetype(const char *path)
{
    /* NetSurf's resource fetcher uses this for default.css, quirks.css and
       user.css.  Calling those octet-stream made the HTML converter reject
       its own base stylesheet and restart forever. */
    return has_css_suffix(path)?"text/css":"application/octet-stream";
}
static nsurl *resource_url(const char *path)
{
    /* A resource: URL here would redirect the resource fetcher back to
       itself.  Unsupported assets must be absent, not self-redirected. */
    (void)path;return 0;
}
static nserror resource_data(const char *path,const uint8_t **data,size_t *length)
{
    /* Only the built-in stylesheets are direct resources.  Supplying CSS for
       icons and other resource paths turns failed assets into fake stylesheets
       and keeps the content cache permanently active. */
    if(!path||!has_css_suffix(path))return NSERROR_NOT_FOUND;
    /* A compact but complete UA sheet.  The first working port used only four
       rules, so ordinary HTML (headings, paragraphs, lists and tables) lost
       virtually all structure before a site's own CSS had loaded. */
    static const char css[]=
        "html,body,div,article,aside,footer,header,main,nav,section{display:block}"
        "head,script,style,title{display:none}"
        "html{background:#f4f6f8}body{margin:0;padding:20px 24px 42px;font-family:sans-serif;font-size:16px;line-height:1.45;color:#20242a;background:#fff}"
        "h1,h2,h3,h4,h5,h6{display:block;font-weight:bold;line-height:1.18;color:#14202b;margin:1.15em 0 .48em}"
        "h1{font-size:2em}h2{font-size:1.55em}h3{font-size:1.25em}h4{font-size:1.08em}h5,h6{font-size:1em}"
        "p{display:block;margin:.72em 0}a:link{color:#075fc4;text-decoration:underline}a:visited{color:#6b3f91}"
        "strong,b{font-weight:bold}em,i{font-style:italic}small{font-size:.84em}pre,code,kbd,samp{font-family:monospace}"
        "pre{display:block;white-space:pre;overflow:hidden;background:#f4f6f8;border:1px solid #d9e0e6;padding:10px;margin:1em 0}"
        "blockquote{display:block;margin:1em 0;padding:4px 16px;border-left:4px solid #4b91dd;color:#44515d}"
        "ul,ol{display:block;margin:.8em 0;padding-left:32px}li{display:list-item;margin:.22em 0}ul{list-style-type:disc}ol{list-style-type:decimal}"
        "hr{display:block;border:0;border-top:1px solid #d7dee5;margin:1.3em 0}"
        "table{display:table;border-collapse:collapse;max-width:100%;margin:1em 0}caption{display:table-caption;font-weight:bold;padding:6px}thead{display:table-header-group}tbody{display:table-row-group}tr{display:table-row}td,th{display:table-cell;padding:7px 9px;border:1px solid #d6dde5;vertical-align:top}th{font-weight:bold;background:#edf3f8}"
        "img{max-width:100%;height:auto;color:#71808d}"
        "form{display:block;margin:1em 0}input,textarea,select,button{font-family:sans-serif;font-size:1em;color:#20242a;background:#fff;border:1px solid #aebbc7;padding:6px 8px;margin:2px}"
        "button,input[type=submit],input[type=button]{background:#1769c2;color:#fff;border-color:#1769c2;border-radius:3px;padding:7px 12px}"
        "article,main,section,header,footer,nav{display:block}";
    *data=(const uint8_t*)css;*length=sizeof(css)-1u;return NSERROR_OK;
}
static nserror release_resource(const uint8_t *data){(void)data;return NSERROR_OK;}
static gui_fetch_table g_fetch_table={fetch_filetype,resource_url,resource_data,release_resource,0};

static nserror option_defaults(nsoption_s *defaults){(void)defaults;return NSERROR_OK;}
static netsurf_table g_netsurf_table={&g_misc_table,&g_window_table,0,0,&g_fetch_table,0,0,0,0,0,&g_bitmap_table,&g_layout_table};

static uint8_t is_space(char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
static uint8_t is_unreserved(char c)
{
    return(c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~';
}
static int append_encoded(char *out,uint32_t cap,uint32_t *at,const char *text)
{
    static const char hex[]="0123456789ABCDEF";
    for(uint32_t i=0u;text&&text[i];++i){
        unsigned char c=(unsigned char)text[i];
        if(is_space((char)c)){if(*at+1u>=cap)return -1;out[(*at)++]='+';}
        else if(is_unreserved((char)c)){if(*at+1u>=cap)return -1;out[(*at)++]=(char)c;}
        else{if(*at+3u>=cap)return -1;out[(*at)++]='%';out[(*at)++]=hex[c>>4];out[(*at)++]=hex[c&15u];}
    }
    out[*at]=0;return 0;
}
static int normalize_address(const char *input,char *out,uint32_t cap)
{
    while(input&&is_space(*input))++input;if(!input||!input[0]||!out||cap<16u)return -1;
    char trimmed[DIHSCOVER_URL_CAP];uint32_t len=0u;
    while(input[len]&&len+1u<sizeof(trimmed))trimmed[len++]=input[len];
    while(len&&is_space(trimmed[len-1u]))--len;trimmed[len]=0;if(!len)return -1;
    uint8_t has_scheme=0u,has_space=0u,has_dot=0u;
    for(uint32_t i=0u;trimmed[i];++i){char c=trimmed[i];if(c==':'&&i>0u&&i<16u){has_scheme=1u;break;}if(is_space(c))has_space=1u;if(c=='.')has_dot=1u;}
    if(has_scheme){dihs_copy(out,cap,trimmed);return dihs_strlen(out)+1u<cap?0:-1;}
    if(has_space||!has_dot){
        const char prefix[]="https://html.duckduckgo.com/html/?q=";uint32_t at=0u;
        for(uint32_t i=0u;prefix[i];++i){if(at+1u>=cap)return -1;out[at++]=prefix[i];}
        return append_encoded(out,cap,&at,trimmed);
    }
    const char prefix[]="https://";uint32_t at=0u;
    for(uint32_t i=0u;prefix[i];++i)out[at++]=prefix[i];
    for(uint32_t i=0u;trimmed[i]&&at+1u<cap;++i)out[at++]=trimmed[i];
    out[at]=0;return trimmed[0]&&at+1u<cap?0:-1;
}

static void destroy_surface(void)
{
    if(g_caret_object)(void)g_api->gfx_obj_destroy(g_caret_object);
    g_caret_object=0u;
    if(g_surface_object)(void)g_api->gfx_obj_destroy(g_surface_object);if(g_surface_image)(void)g_api->img_destroy(g_surface_image);
    g_surface_object=g_surface_image=0u;g_surface_pixels=0;g_surface_stride=0u;
}

static int create_surface(uint32_t width,uint32_t height)
{
    destroy_surface();if(!width||!height)return -1;
    if(g_api->img_create(width,height,0xffffffffu,&g_surface_image)!=0||g_api->img_pixels(g_surface_image,&g_surface_pixels,&g_surface_stride)!=0||
       g_api->gfx_obj_add_image_from_img(g_surface_image,0,0,&g_surface_object)!=0){destroy_surface();return -1;}
    /* The document panel itself is layer 1.  Keep the rendered page above it;
       otherwise a perfectly rendered NetSurf surface is hidden by the panel. */
    (void)g_api->gfx_obj_set_parent(g_surface_object,g_parent);(void)g_api->gfx_obj_set_clip_to_parent(g_surface_object,1u);(void)g_api->gfx_obj_set_z(g_surface_object,2u);(void)g_api->gfx_obj_set_visible(g_surface_object,1u);(void)g_api->gfx_image_set_pos(g_surface_object,0,0);(void)g_api->gfx_image_set_size(g_surface_object,width,height);(void)g_api->gfx_image_set_sample_mode(g_surface_object,SACX_GFX_IMAGE_SAMPLE_NEAREST);
    g_width=width;g_height=height;g_clip={0,0,(int)width,(int)height};g_dirty=1u;return 0;
}

int dihscover_netsurf_init(const sacx_api *api,uint32_t parent,const dihscover_netsurf_callbacks *callbacks)
{
    if(!api||!callbacks||!SACX_API_HAS(api,img_create)||!SACX_API_HAS(api,img_pixels)||!SACX_API_HAS(api,img_touch)||!SACX_API_HAS(api,net_request_start))return -1;
    g_api=api;g_parent=parent;g_callbacks=*callbacks;bitmap_fmt_t format={BITMAP_LAYOUT_ARGB8888,false};bitmap_set_format(&format);
    if(netsurf_register(&g_netsurf_table)!=NSERROR_OK||nsoption_init(option_defaults,&nsoptions,&nsoptions_default)!=NSERROR_OK)return -1;
    /* Use NetSurf's normal browsing behaviour.  GIF and BMP are built into
       this port; the remaining common formats are handled by DihOS next. */
    nsoption_set_bool(foreground_images,true);
    nsoption_set_bool(background_images,true);
    nsoption_set_bool(animate_images,true);
    /* NetSurf draws and handles its own select popup, which is the only
       portable option in the small Dihscover frontend. */
    nsoption_set_bool(core_select_menu,true);
    /* Reflow once the document has useful data instead of laying it out after
       every network fragment on the small cooperative DihOS runtime. */
    nsoption_set_bool(incremental_reflow,false);
    nsoption_set_int(max_fetchers,2);
    if(netsurf_init(0)!=NSERROR_OK)return -1;
    if(create_surface(g_width,g_height)!=0)return -1;if(browser_window_create(BW_CREATE_HISTORY,0,0,0,&g_gui.browser)!=NSERROR_OK)return -1;
    g_initialised=1u;return 0;
}

void dihscover_netsurf_shutdown(void)
{
    if(!g_initialised)return;if(g_gui.browser)browser_window_destroy(g_gui.browser);g_gui.browser=0;netsurf_exit();nsoption_finalise(nsoptions,nsoptions_default);destroy_surface();g_initialised=0u;
}

int dihscover_netsurf_resize(uint32_t width,uint32_t height)
{
    if(width==g_width&&height==g_height&&g_surface_image)return 0;if(!g_initialised){g_width=width;g_height=height;return 0;}if(create_surface(width,height)!=0)return -1;if(g_gui.browser){browser_window_reformat(g_gui.browser,false,(int)width,(int)height);g_dirty=1u;}return 0;
}

int dihscover_netsurf_navigate(const char *address)
{
    if(!g_gui.browser||!address||!address[0])return -1;char normalized[DIHSCOVER_URL_CAP];
    if(normalize_address(address,normalized,sizeof(normalized))!=0)return -1;
    if(g_callbacks.url_changed)g_callbacks.url_changed(normalized);if(g_api&&g_api->log){char text[DIHSCOVER_URL_CAP+32]="Dihscover NetSurf: navigate ";dihs_copy(text+27u,sizeof(text)-27u,normalized);g_api->log(text);}g_render_reports=0u;
    g_page_loading=1u;if(g_callbacks.status_changed)g_callbacks.status_changed("Loading");
    nsurl *target=0;if(nsurl_create(normalized,&target)!=NSERROR_OK)return -1;nserror result=browser_window_navigate(g_gui.browser,target,0,BW_NAVIGATE_HISTORY,0,0,0);nsurl_unref(target);return result==NSERROR_OK?0:-1;
}
int dihscover_netsurf_back(void){return g_gui.browser&&browser_window_history_back(g_gui.browser,false)==NSERROR_OK?0:-1;}
int dihscover_netsurf_forward(void){return g_gui.browser&&browser_window_history_forward(g_gui.browser,false)==NSERROR_OK?0:-1;}
int dihscover_netsurf_reload(void){return g_gui.browser&&browser_window_reload(g_gui.browser,true)==NSERROR_OK?0:-1;}
uint32_t dihscover_netsurf_active_fetches(void){return dihscover_fetch_active_count();}
uint32_t dihscover_netsurf_completed_fetches(void){return dihscover_fetch_completed_count();}
uint32_t dihscover_netsurf_failed_fetches(void){return dihscover_fetch_failed_count();}
uint32_t dihscover_netsurf_page_state(void){if(!g_gui.browser)return CONTENT_STATUS_DONE;hlcache_handle *content=browser_window_get_content(g_gui.browser);if(!content)return g_page_loading?CONTENT_STATUS_LOADING:CONTENT_STATUS_DONE;return(uint32_t)content_get_status(content);}

void dihscover_netsurf_pump(uint64_t now_ticks)
{
    if(!g_initialised)return;g_now=(now_ticks*1000u)/60u;for(uint32_t round=0u;round<2u;++round){for(uint32_t pass=0u;pass<64u;++pass){int found=-1;for(uint32_t i=0u;i<64u;++i)if(g_schedule[i].used&&g_schedule[i].due<=g_now){found=(int)i;break;}if(found<0)break;scheduled_callback call=g_schedule[found];g_schedule[found].used=0u;if(g_api&&g_api->log&&round==1u)g_api->log("Dihscover NetSurf scheduler: ran callback after fetch poll");call.callback(call.context);}if(!round){fd_set readfds,writefds,errorfds;int maxfd=-1;(void)fetch_fdset(&readfds,&writefds,&errorfds,&maxfd);}}
    if(g_dirty&&g_gui.browser&&browser_window_redraw_ready(g_gui.browser)){hlcache_handle *content=browser_window_get_content(g_gui.browser);uint32_t type=content?(uint32_t)content_get_type(content):0u,status=content?(uint32_t)content_get_status(content):0u;g_plot_rects=g_plot_texts=g_plot_bitmaps=0u;fill_rect(0,0,(int)g_width,(int)g_height,0xffffffffu);redraw_context context={true,true,&g_plotters,0};rect clip={0,0,(int)g_width,(int)g_height};bool ok=browser_window_redraw(g_gui.browser,-g_scroll_x,-g_scroll_y,&clip,&context);(void)g_api->img_touch(g_surface_image);if(g_render_reports++<4u)render_report(ok?1u:0u,type,status);g_dirty=0u;}
}

void dihscover_netsurf_mouse(int32_t x,int32_t y,uint8_t buttons,int32_t wheel)
{
    if(!g_gui.browser)return;if(g_callbacks.pointer_changed)g_callbacks.pointer_changed(SACX_MOUSE_CURSOR_ARROW);if(wheel){g_scroll_y-=wheel*48;if(g_scroll_y<0)g_scroll_y=0;g_dirty=1u;}
    browser_mouse_state state=BROWSER_MOUSE_HOVER;if((buttons&1u)&&!(g_previous_buttons&1u)){g_mouse_down_x=x;g_mouse_down_y=y;}
    if(buttons&1u)state=(browser_mouse_state)(state|((g_previous_buttons&1u)&&(x!=g_mouse_down_x||y!=g_mouse_down_y)?BROWSER_MOUSE_DRAG_1:BROWSER_MOUSE_PRESS_1));if((g_previous_buttons&1u)&&!(buttons&1u))state=(browser_mouse_state)(state|BROWSER_MOUSE_CLICK_1);
    browser_window_mouse_track(g_gui.browser,state,x+g_scroll_x,y+g_scroll_y);if(state&BROWSER_MOUSE_CLICK_1)browser_window_mouse_click(g_gui.browser,state,x+g_scroll_x,y+g_scroll_y);g_previous_buttons=buttons;
}

void dihscover_netsurf_key(uint32_t key)
{
    if(g_gui.browser&&key){(void)browser_window_key_press(g_gui.browser,key);g_dirty=1u;}
}
