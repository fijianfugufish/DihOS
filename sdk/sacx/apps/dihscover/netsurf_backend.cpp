#include "netsurf_backend.h"
#include "web_font.h"
#include "preferences.h"
#include "scroll.h"
#include "port/dihscover_profile.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

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
static rect g_clip={0,0,1,1},g_paint_clip={0,0,1,1},g_damage={0,0,1,1};
static uint8_t g_dirty=1u,g_previous_buttons,g_initialised,g_page_loading;
static int32_t g_mouse_down_x,g_mouse_down_y;
static uint64_t g_now;
static scheduled_callback g_schedule[64];
static uint32_t g_plot_rects,g_plot_texts,g_plot_bitmaps;

extern "C" const sacx_api *dihscover_netsurf_api(void){return g_api;}
extern "C" void dihscover_netsurf_log(const char *text){
#ifdef DIHSCOVER_TRACE
    if(g_api&&g_api->log&&text)g_api->log(text);
#else
    (void)text;
#endif
}
extern "C" uint32_t dihscover_fetch_active_count(void);
extern "C" uint32_t dihscover_fetch_completed_count(void);
extern "C" uint32_t dihscover_fetch_failed_count(void);

static uint32_t argb(colour value){return 0xff000000u|((value&0xffu)<<16)|(value&0xff00u)|((value>>16)&0xffu);}
static int maximum(int a,int b){return a>b?a:b;}static int minimum(int a,int b){return a<b?a:b;}
static void damage_all(void){g_dirty=1;g_damage={0,0,(int)g_width,(int)g_height};}
static void damage_area(const rect *area) {
    if(!area){damage_all();return;}
    auto coordinate=[](int p,int scroll,int limit){int64_t v=(int64_t)p-scroll;return v<0?0:v>limit?limit:(int)v;};
    rect r={coordinate(area->x0,g_scroll_x,(int)g_width),coordinate(area->y0,g_scroll_y,(int)g_height),coordinate(area->x1,g_scroll_x,(int)g_width),coordinate(area->y1,g_scroll_y,(int)g_height)};
    if(r.x0>=r.x1||r.y0>=r.y1)return;
    if(!g_dirty)g_damage=r;
    else g_damage={minimum(r.x0,g_damage.x0),minimum(r.y0,g_damage.y0),maximum(r.x1,g_damage.x1),maximum(r.y1,g_damage.y1)};
    g_dirty=1;
}
static uint64_t g_callback_ms,g_paint_ms,g_publish_ms,g_painted_pixels;
#include "netsurf_plotters.inc"

static nserror plot_text(const redraw_context *ctx,const plot_font_style_t *style,int x,int y,const char *text,size_t length)
{
    (void)ctx;++g_plot_texts;
    web_font_paint(style,text,(uint32_t)length,g_surface_pixels,g_surface_stride,x,y,
        maximum(0,g_clip.x0),maximum(0,g_clip.y0),minimum((int)g_width,g_clip.x1),minimum((int)g_height,g_clip.y1));
    return NSERROR_OK;
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
{ *width=web_font_width(style,text,(uint32_t)length);return NSERROR_OK; }
static nserror font_position(const plot_font_style_t *style,const char *text,size_t length,int x,size_t *offset,int *actual)
{ *offset=web_font_position(style,text,(uint32_t)length,x,actual);return NSERROR_OK; }
static nserror font_split(const plot_font_style_t *style,const char *text,size_t length,int x,size_t *offset,int *actual)
{
    font_position(style,text,length,x,offset,actual);size_t fit=*offset;
    while(*offset>0&&text[*offset-1]!=' ')(*offset)--;
    if(!*offset)*offset=fit;
    if(!*offset&&length){*offset=1;while(*offset<length&&((uint8_t)text[*offset]&0xc0)==0x80)++*offset;}
    *actual=web_font_width(style,text,(uint32_t)*offset);return NSERROR_OK;
}
static gui_layout_table g_layout_table={font_width,font_position,font_split};

static nserror schedule_callback(int delay,void (*callback)(void*),void *context)
{
    for(uint32_t i=0;i<64u;++i)if(g_schedule[i].used&&g_schedule[i].callback==callback&&g_schedule[i].context==context)g_schedule[i].used=0u;
    if(delay<0)return NSERROR_OK;for(uint32_t i=0;i<64u;++i)if(!g_schedule[i].used){g_schedule[i].used=1u;g_schedule[i].due=g_now+(uint32_t)delay;g_schedule[i].callback=callback;g_schedule[i].context=context;return NSERROR_OK;}return NSERROR_NOMEM;
}
static gui_misc_table g_misc_table={schedule_callback,0,0,0,0,0};

static gui_window *window_create(browser_window *browser,gui_window *existing,gui_window_create_flags flags){(void)existing;(void)flags;g_gui.browser=browser;return &g_gui;}
static void window_destroy(gui_window *window){if(window==&g_gui)g_gui.browser=0;}
static nserror window_invalidate(gui_window *window,const rect *area){(void)window;damage_area(area);return NSERROR_OK;}
static bool window_get_scroll(gui_window *window,int *x,int *y){(void)window;*x=g_scroll_x;*y=g_scroll_y;return true;}
static void clamp_scroll(int64_t x,int64_t y)
{
    int width=(int)g_width,height=(int)g_height;
    if(g_gui.browser)(void)browser_window_get_extents(g_gui.browser,true,&width,&height);
    g_scroll_x=browser_scroll_clamp(x,width,(int)g_width);
    g_scroll_y=browser_scroll_clamp(y,height,(int)g_height);
}
static nserror window_set_scroll(gui_window *window,const rect *area){(void)window;clamp_scroll(area->x0,area->y0);damage_all();return NSERROR_OK;}
static nserror window_get_dimensions(gui_window *window,int *width,int *height){(void)window;*width=(int)g_width;*height=(int)g_height;return NSERROR_OK;}
static nserror window_event(gui_window *window,gui_window_event event){(void)window;if(event==GW_EVENT_START_THROBBER)g_page_loading=1u;else if(event==GW_EVENT_STOP_THROBBER)g_page_loading=0u;else if(event==GW_EVENT_NEW_CONTENT){g_scroll_x=g_scroll_y=0;g_plot_rects=g_plot_texts=g_plot_bitmaps=0;}else if(event==GW_EVENT_UPDATE_EXTENT)clamp_scroll(g_scroll_x,g_scroll_y);damage_all();return NSERROR_OK;}
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
#include "netsurf_resources.inc"
static nserror resource_data(const char *path,const uint8_t **data,size_t *length)
{
    if(!path||!data||!length)return NSERROR_NOT_FOUND;
    struct entry {const char *name,*data;size_t bytes;};
    static const entry resources[]={
        {"default.css",resource_default_css,sizeof(resource_default_css)-1},
        {"quirks.css",resource_quirks_css,sizeof(resource_quirks_css)-1},
        {"internal.css",resource_internal_css,sizeof(resource_internal_css)-1},
        {"adblock.css",resource_adblock_css,sizeof(resource_adblock_css)-1},
        {"user.css","",0}};
    for(const auto &r:resources)if(dihs_streq(path,r.name)){*data=(const uint8_t*)r.data;*length=r.bytes;return NSERROR_OK;}
    return NSERROR_NOT_FOUND;
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
    g_surface_object=g_surface_image=0u;free(g_surface_pixels);g_surface_pixels=0;g_surface_stride=0u;
}

static int create_surface(uint32_t width,uint32_t height)
{
    if(!width||!height||(uint64_t)width*height>16777216)return -1;
    uint32_t *back=(uint32_t*)malloc((size_t)width*height*4);if(!back)return -1;
    for(uint64_t i=0;i<(uint64_t)width*height;++i)back[i]=0xffffffffu;
    uint32_t image=0,object=0;
    if(g_api->img_create(width,height,0xffffffffu,&image)){free(back);return -1;}
    (void)g_api->sched_preempt_guard_enter();
    if(g_api->gfx_obj_add_image_from_img(image,0,0,&object)) {
        (void)g_api->sched_preempt_guard_leave();g_api->img_destroy(image);free(back);return -1;
    }
    (void)g_api->gfx_obj_set_parent(object,g_parent);(void)g_api->gfx_obj_set_clip_to_parent(object,1u);
    (void)g_api->gfx_obj_set_z(object,2u);(void)g_api->gfx_obj_set_visible(object,1u);
    (void)g_api->gfx_image_set_pos(object,0,0);(void)g_api->gfx_image_set_size(object,width,height);
    (void)g_api->gfx_image_set_sample_mode(object,SACX_GFX_IMAGE_SAMPLE_NEAREST);
    destroy_surface();g_surface_image=image;g_surface_object=object;g_surface_pixels=back;g_surface_stride=width;
    g_width=width;g_height=height;g_clip={0,0,(int)width,(int)height};damage_all();
    (void)g_api->sched_preempt_guard_leave();return 0;
}

int dihscover_netsurf_init(const sacx_api *api,uint32_t parent,const dihscover_netsurf_callbacks *callbacks)
{
    if(!api||!callbacks||!SACX_API_HAS(api,img_create)||!SACX_API_HAS(api,img_pixels)||!SACX_API_HAS(api,img_publish_region)||!SACX_API_HAS(api,net_request_start))return -1;
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
    /* Reflow incremental content; instrumented layout callbacks can suspend. */
    nsoption_set_bool(incremental_reflow,true);
    nsoption_set_int(max_fetchers,2);
    if(api->log)api->log("Dihscover startup: preferences");
    browser_preferences_load(api);
    web_font_preferences(browser_prefs.sans,browser_prefs.serif,browser_prefs.mono,browser_prefs.minimum);
    nsoption_set_int(font_min_size,(browser_prefs.minimum*15)/2); /* tenths of a point */
    if(api->log)api->log("Dihscover startup: packaged fonts");
    if(web_font_init(api)){if(api->log)api->log("Dihscover: packaged web fonts could not be loaded");return -1;}
    if(api->log)api->log("Dihscover startup: engine");
    if(netsurf_init(0)!=NSERROR_OK)return -1;
    if(create_surface(g_width,g_height)!=0)return -1;if(browser_window_create(BW_CREATE_HISTORY,0,0,0,&g_gui.browser)!=NSERROR_OK)return -1;
    (void)browser_window_set_scale(g_gui.browser,browser_prefs.zoom/100.0f,true);
    g_initialised=1u;return 0;
}

void dihscover_netsurf_shutdown(void)
{
    if(!g_initialised)return;if(g_gui.browser)browser_window_destroy(g_gui.browser);g_gui.browser=0;netsurf_exit();web_font_shutdown();nsoption_finalise(nsoptions,nsoptions_default);destroy_surface();g_initialised=0u;
}

int dihscover_netsurf_resize(uint32_t width,uint32_t height)
{
    if(width==g_width&&height==g_height&&g_surface_image)return 0;if(!g_initialised){g_width=width;g_height=height;return 0;}if(create_surface(width,height)!=0)return -1;if(g_gui.browser){browser_window_reformat(g_gui.browser,false,(int)width,(int)height);damage_all();}return 0;
}

int dihscover_netsurf_navigate(const char *address)
{
    if(!g_gui.browser||!address||!address[0])return -1;char normalized[DIHSCOVER_URL_CAP];
    if(normalize_address(address,normalized,sizeof(normalized))!=0)return -1;
    if(g_callbacks.url_changed)g_callbacks.url_changed(normalized);if(g_api&&g_api->log){char text[DIHSCOVER_URL_CAP+32]="Dihscover NetSurf: navigate ";dihs_copy(text+27u,sizeof(text)-27u,normalized);g_api->log(text);}
    g_page_loading=1u;if(g_callbacks.status_changed)g_callbacks.status_changed("Loading");
    nsurl *target=0;if(nsurl_create(normalized,&target)!=NSERROR_OK)return -1;nserror result=browser_window_navigate(g_gui.browser,target,0,BW_NAVIGATE_HISTORY,0,0,0);nsurl_unref(target);return result==NSERROR_OK?0:-1;
}
int dihscover_netsurf_back(void){return g_gui.browser&&browser_window_history_back(g_gui.browser,false)==NSERROR_OK?0:-1;}
int dihscover_netsurf_forward(void){return g_gui.browser&&browser_window_history_forward(g_gui.browser,false)==NSERROR_OK?0:-1;}
int dihscover_netsurf_reload(void){return g_gui.browser&&browser_window_reload(g_gui.browser,true)==NSERROR_OK?0:-1;}
void dihscover_netsurf_stop(void){if(g_gui.browser)browser_window_stop(g_gui.browser);g_page_loading=0;}
void dihscover_netsurf_toggle_images(void)
{
    bool enabled=!nsoption_bool(foreground_images);
    nsoption_set_bool(foreground_images,enabled);nsoption_set_bool(background_images,enabled);
    if(g_gui.browser)(void)browser_window_reload(g_gui.browser,false);
    if(g_callbacks.status_changed)g_callbacks.status_changed(enabled?"Images on":"Images off (F9 to enable)");
}
void dihscover_netsurf_zoom(int delta){int zoom=(int)browser_prefs.zoom+delta;if(zoom<50)zoom=50;if(zoom>300)zoom=300;browser_prefs.zoom=zoom;if(g_gui.browser)(void)browser_window_set_scale(g_gui.browser,zoom/100.0f,true);(void)browser_preferences_save(g_api);damage_all();}
void dihscover_netsurf_diagnostics(void) {
    const char *names[]={"parse","style","layout","paint","publish","fetch dispatch"};
    for(unsigned i=0;i<DIHSCOVER_PHASES;++i){dihscover_phase_stats s=dihscover_profile_stats(i);char line[160];snprintf(line,sizeof line,"Dihscover inclusive active ms: %s calls=%u total=%u max=%u",names[i],(unsigned)s.calls,(unsigned)s.total_ms,(unsigned)s.max_ms);if(g_api&&g_api->log)g_api->log(line);}

    char text[240];snprintf(text,sizeof text,"Dihscover: callbacks active ms=%u paint active ms=%u publish ms=%u painted pixels=%u heap used=%u peak=%u",
      (unsigned)g_callback_ms,(unsigned)g_paint_ms,(unsigned)g_publish_ms,(unsigned)g_painted_pixels,(unsigned)browser_heap_used(),(unsigned)browser_heap_peak());
    if(g_api&&g_api->log)g_api->log(text);
    int doc_width=0,doc_height=0;
    if(g_gui.browser)(void)browser_window_get_extents(g_gui.browser,true,&doc_width,&doc_height);
    uint32_t nonwhite=0;
    if(g_surface_pixels)for(uint32_t y=0;y<g_height;y+=8)for(uint32_t x=0;x<g_width;x+=8)
        if((g_surface_pixels[(uint64_t)y*g_surface_stride+x]&0xffffffu)!=0xffffffu)++nonwhite;
    snprintf(text,sizeof text,"Dihscover: state=%u images=%u view=%ux%u doc=%dx%d scroll=%d,%d text=%u rect=%u bitmap=%u nonwhite=%u",
        (unsigned)dihscover_netsurf_page_state(),(unsigned)nsoption_bool(foreground_images),g_width,g_height,doc_width,doc_height,g_scroll_x,g_scroll_y,g_plot_texts,g_plot_rects,g_plot_bitmaps,nonwhite);
    if(g_api&&g_api->log)g_api->log(text);if(g_callbacks.status_changed)g_callbacks.status_changed(text);
}
void dihscover_netsurf_cycle_font(void) {
    browser_prefs.sans=(browser_prefs.sans+1)%3;
    web_font_preferences(browser_prefs.sans,browser_prefs.serif,browser_prefs.mono,browser_prefs.minimum);
    /* Text box min/max widths are cached by NetSurf. Recreate the document
     * after changing font metrics rather than reusing those old widths. */
    if(g_gui.browser)(void)browser_window_reload(g_gui.browser,false);
    (void)browser_preferences_save(g_api);damage_all();
    const char *labels[]={"Default font: Noto Sans","Default font: Noto Serif","Default font: Noto Sans Mono"};
    if(g_callbacks.status_changed)g_callbacks.status_changed(labels[browser_prefs.sans]);
}
uint32_t dihscover_netsurf_active_fetches(void){return dihscover_fetch_active_count();}
uint32_t dihscover_netsurf_completed_fetches(void){return dihscover_fetch_completed_count();}
uint32_t dihscover_netsurf_failed_fetches(void){return dihscover_fetch_failed_count();}
uint32_t dihscover_netsurf_page_state(void){if(!g_gui.browser)return CONTENT_STATUS_DONE;hlcache_handle *content=browser_window_get_content(g_gui.browser);if(!content)return g_page_loading?CONTENT_STATUS_LOADING:CONTENT_STATUS_DONE;return(uint32_t)content_get_status(content);}

void dihscover_netsurf_pump(uint64_t now_ticks)
{
    if(!g_initialised)return;g_now=SACX_API_HAS(g_api,time_monotonic_ms)?g_api->time_monotonic_ms():(now_ticks*1000u)/60u;for(uint32_t round=0u;round<2u;++round){for(uint32_t pass=0u;pass<64u;++pass){int found=-1;for(uint32_t i=0u;i<64u;++i)if(g_schedule[i].used&&g_schedule[i].due<=g_now){found=(int)i;break;}if(found<0)break;scheduled_callback call=g_schedule[found];g_schedule[found].used=0u;uint64_t began=dihscover_active_ms();call.callback(call.context);g_callback_ms+=dihscover_active_ms()-began;}if(!round){fd_set readfds,writefds,errorfds;int maxfd=-1;uint64_t fetch_began=dihscover_profile_begin(DIHSCOVER_FETCH);(void)fetch_fdset(&readfds,&writefds,&errorfds,&maxfd);dihscover_profile_end(DIHSCOVER_FETCH,fetch_began);}}
    if(g_dirty&&g_gui.browser&&browser_window_redraw_ready(g_gui.browser)) {
        rect clip=g_damage;g_dirty=0;g_paint_clip=clip;g_clip=clip;
        uint64_t began=dihscover_profile_begin(DIHSCOVER_PAINT);
        fill_rect(clip.x0,clip.y0,clip.x1,clip.y1,0xffffffffu);
        redraw_context context={true,true,&g_plotters,0};
        (void)browser_window_redraw(g_gui.browser,-g_scroll_x,-g_scroll_y,&clip,&context);
        uint64_t painted=dihscover_active_ms();g_paint_ms+=painted-began;dihscover_profile_end(DIHSCOVER_PAINT,began);
        (void)g_api->img_publish_region(g_surface_image,g_surface_pixels,g_surface_stride,clip.x0,clip.y0,clip.x1-clip.x0,clip.y1-clip.y0);
        g_publish_ms+=dihscover_active_ms()-painted;dihscover_profile_end(DIHSCOVER_PUBLISH,painted);
        g_painted_pixels+=(uint64_t)(clip.x1-clip.x0)*(clip.y1-clip.y0);
    }
}

#include "netsurf_mouse.inc"

void dihscover_netsurf_key(uint32_t key)
{
    if(g_gui.browser&&key){(void)browser_window_key_press(g_gui.browser,key);damage_all();}
}
