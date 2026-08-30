#include "netsurf_backend.h"
#include "sacx_keys.h"
extern "C" {
#include "netsurf/keypress.h"
}

enum { ACTION_NONE, ACTION_BACK, ACTION_FORWARD, ACTION_RELOAD, ACTION_GO };
enum { UI_TITLEBAR_H=34, UI_TOOLBAR_Y=42, UI_STATUS_Y=84, UI_PROGRESS_Y=101, UI_DOCUMENT_Y=112 };
enum { DIHSCOVER_CONTENT_LOADING, DIHSCOVER_CONTENT_READY, DIHSCOVER_CONTENT_DONE };

typedef struct toolbar_button { uint32_t button, root, label; } toolbar_button;

static const sacx_api *g_api;
static uint32_t g_window, g_root, g_viewport, g_status_obj;
static uint32_t g_progress_bg, g_progress_fill;
static uint32_t g_address, g_address_root;
static toolbar_button g_buttons[4];
static uint32_t g_action, g_pressed_mask;
static uint32_t g_root_w, g_root_h, g_view_w, g_view_h;
static int32_t g_root_x, g_root_y;
static uint8_t g_previous_mouse, g_previous_address_focus;
static uint8_t g_loading;
static uint64_t g_load_started;
static uint32_t g_last_done, g_last_failed;
static char g_status_text[160];

static sacx_color rgb(uint8_t r,uint8_t g,uint8_t b){sacx_color c={r,g,b};return c;}

static void set_status(const char *text)
{
    dihs_copy(g_status_text,sizeof(g_status_text),text?text:"");
    if(g_status_obj)(void)g_api->gfx_text_set(g_status_obj,g_status_text);
}

static void title_changed(const char *title)
{
    (void)g_api->window_set_title(g_window,(title&&title[0])?title:"Dihscover");
}

static void url_changed(const char *url)
{
    if(url)(void)g_api->textbox_set_text(g_address,url);
}

static void pointer_changed(uint32_t pointer)
{
    (void)g_api->mouse_set_cursor(pointer);
}

static void append_char(char *dst,uint32_t cap,char c)
{
    uint32_t n=dihs_strlen(dst);if(n+1u<cap){dst[n]=c;dst[n+1u]=0;}
}

static void append_text(char *dst,uint32_t cap,const char *src)
{
    uint32_t n=dihs_strlen(dst);for(uint32_t i=0u;src&&src[i]&&n+1u<cap;++i)dst[n++]=src[i];dst[n]=0;
}

static void append_uint(char *dst,uint32_t cap,uint32_t value)
{
    char tmp[12];uint32_t n=0u;if(!value)tmp[n++]='0';
    while(value&&n<sizeof(tmp)){tmp[n++]=(char)('0'+(value%10u));value/=10u;}
    while(n)append_char(dst,cap,tmp[--n]);
}

static void mark_loading(const char *status)
{
    g_loading=1u;g_load_started=g_api?g_api->time_ticks():0u;
    g_last_done=dihscover_netsurf_completed_fetches();g_last_failed=dihscover_netsurf_failed_fetches();
    set_status(status?status:"Loading");
}

static void button_cb(uint32_t handle,void *user)
{
    (void)handle;g_action=(uint32_t)(uintptr_t)user;
}

static int make_button(toolbar_button *button,const char *label,uint32_t action)
{
    sacx_button_style style=sacx_button_style_default();
    style.fill=rgb(45,52,60);style.hover_fill=rgb(62,72,82);style.pressed_fill=rgb(24,126,148);
    style.outline=rgb(105,119,130);style.outline_width=1u;
    if(g_api->button_add_rect(0,0,32,30,20,&style,button_cb,(void*)(uintptr_t)action,&button->button)!=0||
       g_api->button_root(button->button,&button->root)!=0||
       g_api->gfx_obj_add_text(label,16,9,1,rgb(242,246,248),255,1,0,0,SACX_TEXT_ALIGN_CENTER,1,&button->label)!=0)return -1;
    (void)g_api->gfx_obj_set_parent(button->root,g_root);
    (void)g_api->gfx_obj_set_parent(button->label,button->root);
    return 0;
}

static void layout_ui(void)
{
    int32_t x=0,y=0;uint32_t w=0,h=0;
    if(g_api->gfx_obj_get_rect(g_root,&x,&y,&w,&h)!=0)return;
    g_root_x=x;g_root_y=y;g_root_w=w;g_root_h=h;
    g_view_w=w;g_view_h=h>UI_DOCUMENT_Y?h-UI_DOCUMENT_Y:1u;
    for(uint32_t i=0;i<3u;++i){(void)g_api->gfx_obj_set_rect(g_buttons[i].root,8+(int32_t)i*38,UI_TOOLBAR_Y,32,30);(void)g_api->gfx_text_set_pos(g_buttons[i].label,16,0);}
    (void)g_api->gfx_obj_set_rect(g_buttons[3].root,(int32_t)w-52,UI_TOOLBAR_Y,44,30);
    (void)g_api->gfx_text_set_pos(g_buttons[3].label,22,0);
    (void)g_api->textbox_set_bounds(g_address,122,UI_TOOLBAR_Y,w>190u?w-190u:40u,30);
    (void)g_api->gfx_obj_set_rect(g_viewport,0,UI_DOCUMENT_Y,w,g_view_h);
    if(g_progress_bg)(void)g_api->gfx_obj_set_rect(g_progress_bg,10,UI_PROGRESS_Y,w>20u?w-20u:1u,4u);
    (void)g_api->gfx_text_set_pos(g_status_obj,10,UI_STATUS_Y);
    (void)dihscover_netsurf_resize(g_view_w,g_view_h);
}

static void poll_toolbar(void)
{
    static const uint32_t actions[4]={ACTION_BACK,ACTION_FORWARD,ACTION_RELOAD,ACTION_GO};
    uint32_t pressed=0;
    for(uint32_t i=0;i<4u;++i)if(g_api->button_pressed(g_buttons[i].button))pressed|=1u<<i;
    uint32_t released=g_pressed_mask&~pressed;
    for(uint32_t i=0;i<4u;++i)if(released&(1u<<i)){g_action=actions[i];break;}
    g_pressed_mask=pressed;
    uint8_t focused=(uint8_t)(g_api->textbox_focused(g_address)>0);
    if(focused&&!g_previous_address_focus&&SACX_API_HAS(g_api,textbox_select))
        (void)g_api->textbox_select(g_address,0u,DIHSCOVER_URL_CAP-1u);
    g_previous_address_focus=focused;
    if(focused&&(g_api->input_key_pressed(SACX_KEY_ENTER)||g_api->input_key_pressed(SACX_KEY_KP_ENTER)))g_action=ACTION_GO;
}

static uint32_t netsurf_key_for_usage(uint8_t usage,uint8_t shift)
{
    if(usage>=SACX_KEY_A&&usage<=SACX_KEY_Z)return (uint32_t)((shift?'A':'a')+usage-SACX_KEY_A);
    if(usage>=SACX_KEY_1&&usage<=SACX_KEY_9){static const char normal[]="123456789",shifted[]="!@#$%^&*(";return(uint8_t)(shift?shifted[usage-SACX_KEY_1]:normal[usage-SACX_KEY_1]);}
    if(usage==SACX_KEY_0)return shift?')':'0';
    switch(usage){
    case SACX_KEY_SPACE:return ' ';case SACX_KEY_MINUS:return shift?'_':'-';case SACX_KEY_EQUAL:return shift?'+':'=';
    case SACX_KEY_LEFTBRACE:return shift?'{':'[';case SACX_KEY_RIGHTBRACE:return shift?'}':']';case SACX_KEY_BACKSLASH:return shift?'|':'\\';
    case SACX_KEY_SEMICOLON:return shift?':':';';case SACX_KEY_APOSTROPHE:return shift?'\"':'\'';case SACX_KEY_GRAVE:return shift?'~':'`';
    case SACX_KEY_COMMA:return shift?'<':',';case SACX_KEY_DOT:return shift?'>':'.';case SACX_KEY_SLASH:return shift?'?':'/';
    case SACX_KEY_BACKSPACE:return NS_KEY_DELETE_LEFT;case SACX_KEY_DELETE:return NS_KEY_DELETE_RIGHT;case SACX_KEY_ENTER:case SACX_KEY_KP_ENTER:return NS_KEY_CR;
    case SACX_KEY_TAB:return shift?NS_KEY_SHIFT_TAB:NS_KEY_TAB;case SACX_KEY_ESCAPE:return NS_KEY_ESCAPE;case SACX_KEY_LEFT:return NS_KEY_LEFT;
    case SACX_KEY_RIGHT:return NS_KEY_RIGHT;case SACX_KEY_UP:return NS_KEY_UP;case SACX_KEY_DOWN:return NS_KEY_DOWN;case SACX_KEY_HOME:return NS_KEY_LINE_START;
    case SACX_KEY_END:return NS_KEY_LINE_END;case SACX_KEY_PAGEUP:return NS_KEY_PAGE_UP;case SACX_KEY_PAGEDOWN:return NS_KEY_PAGE_DOWN;default:return 0u;}
}

static void poll_page_keys(void)
{
    if(g_api->textbox_focused(g_address)>0)return;
    uint8_t shift=(g_api->input_key_down(SACX_KEY_LSHIFT)||g_api->input_key_down(SACX_KEY_RSHIFT))?1u:0u;
    /* Letter/punctuation HID usages only.  Control usages below are handled
       separately; scanning both was sending Backspace twice. */
    for(uint8_t usage=SACX_KEY_A;usage<=SACX_KEY_0;++usage)if(g_api->input_key_pressed(usage)){uint32_t key=netsurf_key_for_usage(usage,shift);if(key)dihscover_netsurf_key(key);}
    static const uint8_t punctuation[]={SACX_KEY_SPACE,SACX_KEY_MINUS,SACX_KEY_EQUAL,SACX_KEY_LEFTBRACE,SACX_KEY_RIGHTBRACE,SACX_KEY_BACKSLASH,SACX_KEY_SEMICOLON,SACX_KEY_APOSTROPHE,SACX_KEY_GRAVE,SACX_KEY_COMMA,SACX_KEY_DOT,SACX_KEY_SLASH};
    for(uint32_t i=0;i<sizeof(punctuation);++i)if(g_api->input_key_pressed(punctuation[i])){uint32_t key=netsurf_key_for_usage(punctuation[i],shift);if(key)dihscover_netsurf_key(key);}
    static const uint8_t special[]={SACX_KEY_BACKSPACE,SACX_KEY_DELETE,SACX_KEY_ENTER,SACX_KEY_KP_ENTER,SACX_KEY_TAB,SACX_KEY_ESCAPE,SACX_KEY_LEFT,SACX_KEY_RIGHT,SACX_KEY_UP,SACX_KEY_DOWN,SACX_KEY_HOME,SACX_KEY_END,SACX_KEY_PAGEUP,SACX_KEY_PAGEDOWN};
    for(uint32_t i=0;i<sizeof(special);++i)if(g_api->input_key_pressed(special[i])){uint32_t key=netsurf_key_for_usage(special[i],shift);if(key)dihscover_netsurf_key(key);}
}

static void handle_action(void)
{
    uint32_t action=g_action;g_action=ACTION_NONE;
    if(action==ACTION_GO){char url[DIHSCOVER_URL_CAP];if(g_api->textbox_text_copy(g_address,url,sizeof(url))>0){mark_loading("Loading");(void)dihscover_netsurf_navigate(url);}}
    else if(action==ACTION_BACK){mark_loading("Going back");(void)dihscover_netsurf_back();}
    else if(action==ACTION_FORWARD){mark_loading("Going forward");(void)dihscover_netsurf_forward();}
    else if(action==ACTION_RELOAD){mark_loading("Reloading");(void)dihscover_netsurf_reload();}
}

static void update_progress(uint64_t now)
{
    uint32_t active=dihscover_netsurf_active_fetches();
    uint32_t done=dihscover_netsurf_completed_fetches();
    uint32_t failed=dihscover_netsurf_failed_fetches();
    uint32_t page_state=dihscover_netsurf_page_state();
    uint8_t page_loading=page_state==DIHSCOVER_CONTENT_LOADING||page_state==DIHSCOVER_CONTENT_READY;
    if(active||page_loading){
        if(!g_loading){g_loading=1u;g_load_started=now;g_last_done=done;g_last_failed=failed;}
        uint32_t elapsed=(uint32_t)((now-g_load_started)/1000u);
        char text[160];text[0]=0;append_text(text,sizeof(text),active?"Loading ":"Finishing ");
        append_uint(text,sizeof(text),active);append_text(text,sizeof(text)," active, ");
        append_uint(text,sizeof(text),done-g_last_done);append_text(text,sizeof(text)," done");
        if(failed!=g_last_failed){append_text(text,sizeof(text),", ");append_uint(text,sizeof(text),failed-g_last_failed);append_text(text,sizeof(text)," failed");}
        append_text(text,sizeof(text),"  ");append_uint(text,sizeof(text),elapsed);append_char(text,sizeof(text),'s');
        if(g_status_obj)(void)g_api->gfx_text_set(g_status_obj,text);
    }else if(done!=g_last_done||failed!=g_last_failed){
        g_loading=0u;g_last_done=done;g_last_failed=failed;set_status(failed?"Load failed":"Ready");
    }else if(g_loading){
        g_loading=0u;g_last_done=done;g_last_failed=failed;set_status("Ready");
    }
    if(g_progress_bg)(void)g_api->gfx_obj_set_visible(g_progress_bg,g_loading?1u:0u);
    if(g_progress_fill){
        (void)g_api->gfx_obj_set_visible(g_progress_fill,g_loading?1u:0u);
        if(g_loading){
            uint32_t width=g_root_w>20u?g_root_w-20u:1u;
            uint32_t fill=width/5u;if(fill<36u)fill=36u;if(fill>width)fill=width;
            uint32_t range=width>fill?width-fill:1u;
            uint32_t phase=(uint32_t)((now/18u)%range);
            (void)g_api->gfx_obj_set_rect(g_progress_fill,10+(int32_t)phase,UI_PROGRESS_Y,fill,4u);
        }
    }
}

static int update(const sacx_api *api)
{
    if(!api->window_visible(g_window)){dihscover_netsurf_shutdown();return api->app_exit(0,"Dihscover closed");}
    uint64_t now=api->time_ticks();layout_ui();poll_toolbar();poll_page_keys();handle_action();dihscover_netsurf_pump(now);update_progress(now);
    sacx_mouse_state mouse;
    if(api->mouse_get_state(&mouse)==0&&api->window_focused(g_window)){
        int32_t local_x=mouse.x-g_root_x,local_y=mouse.y-g_root_y;
        if(local_y>=UI_DOCUMENT_Y)dihscover_netsurf_mouse(local_x,local_y-UI_DOCUMENT_Y,mouse.buttons,mouse.wheel);
        else (void)g_api->mouse_set_cursor(SACX_MOUSE_CURSOR_ARROW);
        g_previous_mouse=mouse.buttons;
    }
    return api->app_yield();
}

static int create_ui(void)
{
    sacx_window_style window_style=sacx_window_style_default();
    sacx_textbox_style textbox_style=sacx_textbox_style_default();
    window_style.body_fill=rgb(245,247,248);window_style.body_outline=rgb(55,66,74);
    window_style.titlebar_fill=rgb(30,38,45);window_style.title_color=rgb(245,248,250);window_style.titlebar_height=UI_TITLEBAR_H;
    if(g_api->window_create_ex(90,56,960,680,28,"Dihscover",&window_style,&g_window)!=0||g_api->window_root(g_window,&g_root)!=0)return -1;
    (void)g_api->window_set_visible(g_window,1u);
    if(make_button(&g_buttons[0],"<",ACTION_BACK)||make_button(&g_buttons[1],">",ACTION_FORWARD)||make_button(&g_buttons[2],"R",ACTION_RELOAD)||make_button(&g_buttons[3],"Go",ACTION_GO))return -1;
    textbox_style.fill=rgb(255,255,255);textbox_style.focus_fill=rgb(255,255,255);textbox_style.outline=rgb(112,124,134);
    textbox_style.focus_outline=rgb(21,137,166);textbox_style.text_color=rgb(22,28,33);textbox_style.text_scale=1u;
    if(g_api->textbox_add_rect(122,UI_TOOLBAR_Y,750,30,20,&textbox_style,0,0,&g_address)!=0||g_api->textbox_root(g_address,&g_address_root)!=0)return -1;
    (void)g_api->gfx_obj_set_parent(g_address_root,g_root);(void)g_api->textbox_set_max_len(g_address,DIHSCOVER_URL_CAP-1u);
    if(g_api->gfx_obj_add_rect(0,UI_DOCUMENT_Y,960,574,1,rgb(255,255,255),1,&g_viewport)!=0)return -1;
    (void)g_api->gfx_obj_set_parent(g_viewport,g_root);(void)g_api->gfx_obj_set_clip_to_parent(g_viewport,1u);
    if(g_api->gfx_obj_add_rect(10,UI_PROGRESS_Y,940,4,30,rgb(216,223,228),0,&g_progress_bg)!=0||
       g_api->gfx_obj_add_rect(10,UI_PROGRESS_Y,120,4,31,rgb(21,137,166),0,&g_progress_fill)!=0)return -1;
    (void)g_api->gfx_obj_set_parent(g_progress_bg,g_root);(void)g_api->gfx_obj_set_parent(g_progress_fill,g_root);
    if(g_api->gfx_obj_add_text("Starting NetSurf",10,UI_STATUS_Y,20,rgb(73,84,92),255,1,0,0,SACX_TEXT_ALIGN_LEFT,1,&g_status_obj)!=0)return -1;
    (void)g_api->gfx_obj_set_parent(g_status_obj,g_root);
    layout_ui();return 0;
}

extern "C" int sacx_main(const sacx_api *api)
{
    if(!api||api->abi_version!=SACX_API_ABI_VERSION)return -1;
    g_api=api;(void)SACX_APP_NO_CONSOLE(api);browser_heap_set_api(api);
    if(browser_heap_init()!=0)return api->app_exit(-1,"Dihscover heap allocation failed");
    if(create_ui()!=0)return api->app_exit(-1,"Dihscover UI failed");
    dihscover_netsurf_callbacks callbacks={title_changed,url_changed,set_status,pointer_changed};
    if(dihscover_netsurf_init(api,g_viewport,&callbacks)!=0)return api->app_exit(-1,"NetSurf backend initialisation failed");
    (void)g_api->textbox_set_text(g_address,"https://www.netsurf-browser.org/");
    set_status("Ready");
    if(api->app_set_update(update)!=0)return api->app_exit(-1,"Dihscover update failed");
    return 0;
}
