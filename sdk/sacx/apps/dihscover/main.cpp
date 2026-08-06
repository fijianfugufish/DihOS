#include "browser.h"

enum { ACTION_NONE, ACTION_BACK, ACTION_FORWARD, ACTION_RELOAD, ACTION_GO };
enum { LOAD_IDLE, LOAD_DOCUMENT, LOAD_STYLES, LOAD_IMAGES };
enum {
    UI_TITLEBAR_H = 34,
    UI_TOOLBAR_Y = 42,
    UI_STATUS_Y = 84,
    UI_DOCUMENT_Y = 106
};

typedef struct toolbar_button { uint32_t button, root, label; } toolbar_button;
typedef struct browser_stylesheet { uint8_t used, loaded, failed, reserved; char url[DIHSCOVER_URL_CAP]; } browser_stylesheet;

static const sacx_api *g_api;
static browser_document g_doc;
static browser_history_entry g_history[DIHSCOVER_HISTORY_CAP];
static browser_image g_images[DIHSCOVER_IMAGE_CAP];
static browser_paint g_paint[DIHSCOVER_PAINT_CAP];
static browser_stylesheet g_styles[8];
static uint8_t g_body[DIHSCOVER_BODY_CAP];
static uint8_t *g_page_cache;
static uint32_t g_page_cache_size;
static char g_page_cache_url[DIHSCOVER_URL_CAP];
static uint8_t g_page_cache_tls_unverified;
static uint32_t g_window, g_root, g_viewport, g_page_bg, g_status_obj, g_tls_obj;
static uint32_t g_textbox, g_textbox_root, g_page_textbox, g_page_textbox_root, g_request, g_generation = 1u;
static uint32_t g_retired_requests[8];
static toolbar_button g_buttons[4];
static uint32_t g_action, g_load_state, g_history_count, g_history_at;
static uint32_t g_paint_count;
static uint32_t g_button_pressed_mask;
static uint8_t g_network_ready;
static int32_t g_scroll_y, g_root_x, g_root_y;
static uint32_t g_root_w, g_root_h, g_view_w, g_view_h;
static uint8_t g_prev_mouse;
static uint8_t g_prev_address_focus;
static char g_status[192];
static uint8_t g_suppress_document_click;
static uint8_t g_document_tls_unverified;
static int32_t g_page_input_node = -1;
static uint8_t g_page_submit_pending;

static sacx_color rgb(uint8_t r, uint8_t g, uint8_t b) { sacx_color c={r,g,b}; return c; }

static void set_status(const char *text)
{
    b_copy(g_status,sizeof(g_status),text);
    if (g_status_obj) (void)g_api->gfx_text_set(g_status_obj,g_status);
}

static void append_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t n=b_strlen(dst), i=0u;
    while (src && src[i] && n+1u<cap) dst[n++]=src[i++];
    dst[n]=0;
}

static int contains_text(const char *text, const char *needle)
{
    uint32_t z=b_strlen(needle);if(!text||!z)return 0;for(uint32_t i=0u;text[i];++i){uint32_t k=0u;while(k<z&&text[i+k]==needle[k])++k;if(k==z)return 1;}return 0;
}

static void button_cb(uint32_t handle, void *user)
{
    (void)handle;
    g_action=(uint32_t)(uintptr_t)user;
}

static void page_textbox_submit(uint32_t handle,const char*text,void*user)
{
    (void)handle;(void)user;if(g_page_input_node>0){(void)browser_document_set_attribute(&g_doc,(uint32_t)g_page_input_node,"value",text?text:"",0u);g_page_submit_pending=1u;}
}

static void close_page_input(void)
{
    if(g_page_input_node>0&&g_page_textbox){char value[512];if(g_api->textbox_text_copy(g_page_textbox,value,sizeof(value))>=0)(void)browser_document_set_attribute(&g_doc,(uint32_t)g_page_input_node,"value",value,0u);}
    g_page_input_node=-1;g_page_submit_pending=0u;if(g_page_textbox_root)(void)g_api->gfx_obj_set_visible(g_page_textbox_root,0u);
}

static int make_button(toolbar_button *b, const char *label, uint32_t action)
{
    sacx_button_style s=sacx_button_style_default();
    s.fill=rgb(45,52,60); s.hover_fill=rgb(62,72,82); s.pressed_fill=rgb(24,126,148);
    s.outline=rgb(105,119,130); s.outline_width=1u;
    if (g_api->button_add_rect(0,0,32,30,20,&s,button_cb,(void *)(uintptr_t)action,&b->button)!=0 ||
        g_api->button_root(b->button,&b->root)!=0 ||
        g_api->gfx_obj_add_text(label,16,9,1,rgb(242,246,248),255,1,0,0,SACX_TEXT_ALIGN_CENTER,1,&b->label)!=0) return -1;
    (void)g_api->gfx_obj_set_parent(b->root,g_root);
    (void)g_api->gfx_obj_set_parent(b->label,b->root);
    return 0;
}

static void clear_paint(void)
{
    for (uint32_t i=0u;i<DIHSCOVER_PAINT_CAP;++i) {
        g_paint[i].active=0u; g_paint[i].node=-1;
        if (g_paint[i].object) (void)g_api->gfx_obj_set_visible(g_paint[i].object,0u);
        if (g_paint[i].underline_object) (void)g_api->gfx_obj_set_visible(g_paint[i].underline_object,0u);
        if (g_paint[i].underline_object2) (void)g_api->gfx_obj_set_visible(g_paint[i].underline_object2,0u);
        if (g_paint[i].underline_object3) (void)g_api->gfx_obj_set_visible(g_paint[i].underline_object3,0u);
        if (g_paint[i].background_object) (void)g_api->gfx_obj_set_visible(g_paint[i].background_object,0u);
    }
}

static void clear_images(void)
{
    for (uint32_t i=0u;i<DIHSCOVER_IMAGE_CAP;++i) {
        if (g_images[i].request) {
            (void)g_api->net_request_cancel(g_images[i].request);
            for (uint32_t k=0u;k<8u;++k) if(!g_retired_requests[k]) { g_retired_requests[k]=g_images[i].request; break; }
        }
        if (g_images[i].object) (void)g_api->gfx_obj_destroy(g_images[i].object);
        if (g_images[i].image) (void)g_api->img_destroy(g_images[i].image);
        b_memset(&g_images[i],0,sizeof(g_images[i]));
    }
}

static void cancel_load(void)
{
    close_page_input();
    if (g_request) {
        (void)g_api->net_request_cancel(g_request);
        for (uint32_t k=0u;k<8u;++k) if(!g_retired_requests[k]) { g_retired_requests[k]=g_request; break; }
        g_request=0u;
    }
    clear_images();
    browser_scripts_stop();
}

static void history_save_scroll(void)
{
    if (g_history_count && g_history_at<g_history_count) g_history[g_history_at].scroll_y=g_scroll_y;
}

static void history_push(const char *url)
{
    history_save_scroll();
    if (g_history_at+1u<g_history_count) g_history_count=g_history_at+1u;
    if (g_history_count==DIHSCOVER_HISTORY_CAP) {
        for (uint32_t i=1u;i<g_history_count;++i) g_history[i-1u]=g_history[i];
        --g_history_count;
    }
    g_history_at=g_history_count++;
    b_copy(g_history[g_history_at].url,sizeof(g_history[g_history_at].url),url);
    g_history[g_history_at].scroll_y=0;
}

static int start_request(const char *url)
{
    sacx_net_request_desc desc;
    if (!g_network_ready)
        return -1;
    b_memset(&desc,0,sizeof(desc));
    desc.url=url; desc.max_response_bytes=DIHSCOVER_BODY_CAP-1u; desc.timeout_ms=45000u; desc.redirect_limit=5u;
    return g_api->net_request_start(&desc,&g_request);
}

static void show_error(const char *title, const char *detail)
{
    char html[768];
    b_copy(html,sizeof(html),"<h1>"); append_text(html,sizeof(html),title); append_text(html,sizeof(html),"</h1><p>");
    append_text(html,sizeof(html),detail); append_text(html,sizeof(html),"</p>");
    browser_document_reset(&g_doc,g_generation,g_history_count?g_history[g_history_at].url:"");
    (void)browser_document_parse(&g_doc,html,b_strlen(html));
    browser_document_layout(&g_doc,g_view_w);
    g_load_state=LOAD_IDLE; set_status(title); clear_paint();
}

static void navigate(const char *input, int push)
{
    char url[DIHSCOVER_URL_CAP];
    if (browser_url_normalize(input,url,sizeof(url))!=0) { show_error("Invalid address","The URL is too long or empty."); return; }
    cancel_load(); ++g_generation; g_scroll_y=0; clear_paint();g_suppress_document_click=1u;
    if (push) history_push(url);
    (void)g_api->textbox_set_text(g_textbox,url);
    browser_document_reset(&g_doc,g_generation,url);
    if (!g_network_ready) { show_error("Browser API unavailable","This kernel does not export the Dihscover network API."); return; }
    set_status("Connecting");
    if (start_request(url)!=0) { show_error("Network busy","The previous request has not finished yet."); return; }
    g_load_state=LOAD_DOCUMENT;
}

static void navigate_post(const char *url, const char *body)
{
    sacx_net_request_desc_ex desc;
    if(!SACX_API_HAS(g_api,net_request_start_ex)){show_error("Form submission unavailable","Rebuild the kernel to enable HTTP POST forms.");return;}
    cancel_load();++g_generation;g_scroll_y=0;clear_paint();g_suppress_document_click=1u;history_push(url);(void)g_api->textbox_set_text(g_textbox,url);browser_document_reset(&g_doc,g_generation,url);set_status("Submitting form");
    b_memset(&desc,0,sizeof(desc));desc.url=url;desc.max_response_bytes=DIHSCOVER_BODY_CAP-1u;desc.timeout_ms=45000u;desc.redirect_limit=5u;desc.method="POST";desc.body=body;desc.body_size=b_strlen(body);desc.content_type="application/x-www-form-urlencoded";
    if(g_api->net_request_start_ex(&desc,&g_request)!=0){show_error("Could not submit form","The network request could not be started.");return;}g_load_state=LOAD_DOCUMENT;
}

static int32_t page_input_form(uint32_t node)
{
    for(int32_t p=(int32_t)node;p>0;p=g_doc.nodes[p].parent)if(g_doc.nodes[p].tag==B_TAG_FORM)return p;return -1;
}

static void submit_form(uint32_t form)
{
    char action[DIHSCOVER_URL_CAP],resolved[DIHSCOVER_URL_CAP],method[12],body[DIHSCOVER_FORM_BODY_CAP];
    if(browser_document_encode_form(&g_doc,form,action,sizeof(action),method,sizeof(method),body,sizeof(body))!=0){show_error("Could not submit form","The form data was too large.");return;}
    if(browser_url_resolve(g_doc.base_url,action,resolved,sizeof(resolved))!=0){show_error("Could not submit form","The form action URL was invalid.");return;}
    if((method[0]=='p'||method[0]=='P')&&(method[1]=='o'||method[1]=='O'))navigate_post(resolved,body);
    else{char target[DIHSCOVER_URL_CAP];uint8_t has_query=0u;for(uint32_t i=0u;resolved[i];++i)if(resolved[i]=='?'){has_query=1u;break;}b_copy(target,sizeof(target),resolved);append_text(target,sizeof(target),has_query?"&":"?");append_text(target,sizeof(target),body);navigate(target,1);}
}

static void focus_page_input(uint32_t node)
{
    if(node>=g_doc.node_count||!g_page_textbox)return;close_page_input();g_page_input_node=(int32_t)node;browser_node*n=&g_doc.nodes[node];const char*value=n->value_off?browser_document_string(&g_doc,n->value_off):"";(void)g_api->textbox_set_text(g_page_textbox,value);(void)g_api->textbox_set_bounds(g_page_textbox,n->x,n->y-g_scroll_y,n->w,n->h?n->h:34u);(void)g_api->gfx_obj_set_visible(g_page_textbox_root,1u);(void)g_api->textbox_set_focus(g_page_textbox,1u);if(SACX_API_HAS(g_api,textbox_select))(void)g_api->textbox_select(g_page_textbox,0u,b_strlen(value));
}

static void sync_page_input(void)
{
    if(g_page_input_node<=0||!g_page_textbox)return;if((uint32_t)g_page_input_node>=g_doc.node_count){close_page_input();return;}browser_node*n=&g_doc.nodes[g_page_input_node];int32_t sy=n->y-g_scroll_y;if(!n->h||sy+(int32_t)n->h<0||sy>(int32_t)g_view_h){close_page_input();return;}(void)g_api->textbox_set_bounds(g_page_textbox,n->x,sy,n->w,n->h?n->h:34u);char value[512];if(g_api->textbox_text_copy(g_page_textbox,value,sizeof(value))>=0){const char*old=n->value_off?browser_document_string(&g_doc,n->value_off):"";if(!b_streq(old,value))(void)browser_document_set_attribute(&g_doc,(uint32_t)g_page_input_node,"value",value,0u);}if(g_page_submit_pending){uint32_t input=(uint32_t)g_page_input_node;g_page_submit_pending=0u;int32_t form=page_input_form(input);if(form>0)submit_form((uint32_t)form);}
}

static void pump_retired_requests(void)
{
    if (!g_network_ready) return;
    for(uint32_t i=0u;i<8u;++i)if(g_retired_requests[i]){
        uint32_t st=g_api->net_request_status(g_retired_requests[i]);
        if(st!=SACX_NET_STATUS_QUEUED&&st!=SACX_NET_STATUS_LOADING){(void)g_api->net_request_release(g_retired_requests[i]);g_retired_requests[i]=0u;}
    }
}

static void collect_images(void)
{
    uint32_t count=0u;
    for (uint32_t i=1u;i<g_doc.node_count && count<DIHSCOVER_IMAGE_CAP;++i) if (g_doc.nodes[i].tag==B_TAG_IMG && g_doc.nodes[i].src_off && !(g_doc.nodes[i].attr_width<=2u&&g_doc.nodes[i].attr_height<=2u&&(g_doc.nodes[i].attr_width||g_doc.nodes[i].attr_height))) {
        browser_image *im=&g_images[count];
        im->used=1u; im->node_index=i; im->generation=g_generation;
        const char *src=browser_document_string(&g_doc,g_doc.nodes[i].src_off);
        if(contains_text(src,"/ip3/")||contains_text(src,".ico")){b_memset(im,0,sizeof(*im));continue;}
        if (browser_url_resolve(g_doc.base_url,src,im->url,sizeof(im->url))!=0) im->failed=1u;
        else g_doc.nodes[i].image_slot=(int16_t)count;
        ++count;
    }
}

static void collect_stylesheets(void)
{
    b_memset(g_styles,0,sizeof(g_styles));uint32_t count=0u;
    for(uint32_t i=1u;i<g_doc.node_count&&count<4u;++i){browser_node*n=&g_doc.nodes[i];if(n->tag!=B_TAG_LINK||!n->href_off||!n->rel_off)continue;const char*rel=browser_document_string(&g_doc,n->rel_off);uint32_t p=0u;int sheet=0;while(rel[p]){while(rel[p]==' '||rel[p]=='\t')++p;uint32_t a=p;while(rel[p]&&rel[p]!=' '&&rel[p]!='\t')++p;if(p-a==10u){sheet=1;for(uint32_t k=0;k<10u;++k){char c=rel[a+k];if(c>='A'&&c<='Z')c=(char)(c+32);if(c!="stylesheet"[k]){sheet=0;break;}}}if(sheet)break;}if(!sheet)continue;const char*href=browser_document_string(&g_doc,n->href_off);if(contains_text(g_doc.base_url,"html.duckduckgo.com")&&contains_text(href,"duckduckgo.com"))continue;browser_stylesheet*s=&g_styles[count++];s->used=1u;if(browser_url_resolve(g_doc.base_url,href,s->url,sizeof(s->url))!=0)s->failed=1u;}
}

static int start_next_stylesheet(void)
{
    for(uint32_t i=0u;i<8u;++i)if(g_styles[i].used&&!g_styles[i].loaded&&!g_styles[i].failed){sacx_net_request_desc d;b_memset(&d,0,sizeof(d));d.url=g_styles[i].url;d.max_response_bytes=512u*1024u;d.timeout_ms=30000u;d.redirect_limit=5u;if(g_api->net_request_start(&d,&g_request)==0){g_load_state=LOAD_STYLES;set_status("Loading stylesheet");return 1;}g_styles[i].failed=1u;}return 0;
}

static void finalize_document(void)
{
    if(g_api->log)g_api->log("Dihscover: layout begin");browser_document_apply_site_defaults(&g_doc);browser_document_layout(&g_doc,g_view_w);if(browser_document_make_readable(&g_doc)){if(g_api->log)g_api->log("Dihscover: readable fallback");browser_document_layout(&g_doc,g_view_w);}if(g_api->log)g_api->log("Dihscover: layout done");b_copy(g_history[g_history_at].url,sizeof(g_history[g_history_at].url),g_doc.url);(void)g_api->textbox_set_text(g_textbox,g_doc.url);(void)g_api->window_set_title(g_window,g_doc.title);(void)g_api->gfx_text_set(g_tls_obj,g_document_tls_unverified?"TLS: provisional":"");clear_images();collect_images();if(g_api->log)g_api->log("Dihscover: scripts begin");(void)browser_scripts_start(&g_doc,g_api->time_ticks());if(g_api->log)g_api->log("Dihscover: document ready");g_load_state=LOAD_IMAGES;set_status(g_doc.truncated?"Loaded safely (truncated)":"Loaded");
}

static void finish_document(void)
{
    sacx_net_response_info info; uint32_t off=0u,read_size;uint8_t forced_truncation=0u;
    b_memset(&info,0,sizeof(info));
    if (g_api->net_response_info(g_request,&info)!=0 || info.body_size>=DIHSCOVER_BODY_CAP) { show_error("Response too large","Dihscover stopped the download at its page limit."); return; }
    read_size=info.body_size;if(contains_text(info.final_url,"wikipedia.org")&&read_size>768u*1024u){read_size=768u*1024u;forced_truncation=1u;}
    while (off<read_size) { uint32_t got=0u; if(g_api->net_response_read(g_request,off,g_body+off,read_size-off,&got)!=0||!got) break; off+=got; }
    g_body[off]=0; (void)g_api->net_request_release(g_request); g_request=0u;
    browser_document_reset(&g_doc,g_generation,info.final_url[0]?info.final_url:g_history[g_history_at].url);if(g_api->log)g_api->log("Dihscover: parse begin");
    if (browser_document_parse(&g_doc,(const char *)g_body,off)!=0) { show_error("Empty document","The server returned no renderable HTML."); return; }if(forced_truncation)g_doc.truncated=1u;if(g_api->log)g_api->log("Dihscover: parse done");
    if(SACX_API_HAS(g_api,mem_alloc)&&SACX_API_HAS(g_api,mem_free)){
        void *cached=0;if(g_api->mem_alloc(off+1u,&cached)==0&&cached){b_memcpy(cached,g_body,off+1u);if(g_page_cache)(void)g_api->mem_free(g_page_cache);g_page_cache=(uint8_t*)cached;g_page_cache_size=off;b_copy(g_page_cache_url,sizeof(g_page_cache_url),info.final_url[0]?info.final_url:g_history[g_history_at].url);g_page_cache_tls_unverified=info.tls_unverified;}
    }
    g_document_tls_unverified=info.tls_unverified;collect_stylesheets();if(!start_next_stylesheet())finalize_document();
}

static int restore_cached_page(const char *url)
{
    if(!url||!g_page_cache||!g_page_cache_size||!b_streq(url,g_page_cache_url))return 0;
    cancel_load();++g_generation;g_scroll_y=0;clear_paint();g_suppress_document_click=1u;
    (void)g_api->textbox_set_text(g_textbox,url);browser_document_reset(&g_doc,g_generation,url);
    if(browser_document_parse(&g_doc,(const char*)g_page_cache,g_page_cache_size)!=0)return 0;
    g_document_tls_unverified=g_page_cache_tls_unverified;b_memset(g_styles,0,sizeof(g_styles));
    finalize_document();set_status("Restored from history");return 1;
}

static void pump_stylesheets(void)
{
    if(!g_request){if(!start_next_stylesheet())finalize_document();return;}uint32_t st=g_api->net_request_status(g_request);if(st==SACX_NET_STATUS_QUEUED||st==SACX_NET_STATUS_LOADING)return;
    browser_stylesheet*active=0;for(uint32_t i=0;i<8u;++i)if(g_styles[i].used&&!g_styles[i].loaded&&!g_styles[i].failed){active=&g_styles[i];break;}
    if(st==SACX_NET_STATUS_DONE&&active){sacx_net_response_info info;uint32_t got=0u;b_memset(&info,0,sizeof(info));if(g_api->net_response_info(g_request,&info)==0&&info.body_size<DIHSCOVER_BODY_CAP&&g_api->net_response_read(g_request,0u,g_body,info.body_size,&got)==0&&got==info.body_size){g_body[got]=0;(void)browser_document_apply_stylesheet(&g_doc,(const char*)g_body,got);active->loaded=1u;}else active->failed=1u;}else if(active)active->failed=1u;(void)g_api->net_request_release(g_request);g_request=0u;if(!start_next_stylesheet())finalize_document();
}

static int image_near_viewport(const browser_image *im)
{
    const browser_node *n;
    int32_t top=g_scroll_y-(int32_t)g_view_h;
    int32_t bottom=g_scroll_y+(int32_t)g_view_h*2;
    if(!im||im->node_index>=g_doc.node_count)return 0;
    n=&g_doc.nodes[im->node_index];
    return n->y+(int32_t)n->h>=top&&n->y<=bottom;
}

static void relayout_preserving_scroll(void)
{
    int32_t anchor=-1,offset=0;if(g_scroll_y>0){for(uint32_t i=1u;i<g_doc.node_count;++i){browser_node*n=&g_doc.nodes[i];if(n->text_off&&n->h&&n->y+(int32_t)n->h>=g_scroll_y){anchor=(int32_t)i;offset=g_scroll_y-n->y;break;}}}browser_document_layout(&g_doc,g_view_w);if(anchor>0&&(uint32_t)anchor<g_doc.node_count){g_scroll_y=g_doc.nodes[anchor].y+offset;if(g_scroll_y<0)g_scroll_y=0;int32_t max=(int32_t)g_doc.content_height-(int32_t)g_view_h;if(max<0)max=0;if(g_scroll_y>max)g_scroll_y=max;}
}

static int start_next_image(void)
{
    for (uint32_t i=0u;i<DIHSCOVER_IMAGE_CAP;++i) if(g_images[i].used&&!g_images[i].failed&&!g_images[i].image&&!g_images[i].request&&image_near_viewport(&g_images[i])) {
        sacx_net_request_desc d; b_memset(&d,0,sizeof(d)); d.url=g_images[i].url; d.max_response_bytes=DIHSCOVER_BODY_CAP-1u; d.redirect_limit=5u;
        if(g_api->net_request_start(&d,&g_images[i].request)==0) { g_images[i].loading=1u; set_status("Loading image"); return 1; }
        if(!g_images[i].reserved){g_images[i].reserved=1u;set_status("Retrying image");return 1;}g_images[i].failed=1u;return 0;
    }
    return 0;
}

static int reclaim_image_graphics_slot(void)
{
    for(uint32_t i=g_paint_count;i>0u;--i){browser_paint*p=&g_paint[i-1u];uint32_t*h=p->underline_object3?&p->underline_object3:p->underline_object2?&p->underline_object2:p->underline_object?&p->underline_object:p->background_object?&p->background_object:0;if(h){(void)g_api->gfx_obj_destroy(*h);*h=0u;return 1;}}
    if(g_paint_count>96u){browser_paint*p=&g_paint[g_paint_count-1u];if(p->object)(void)g_api->gfx_obj_destroy(p->object);b_memset(p,0,sizeof(*p));--g_paint_count;return 1;}return 0;
}

static int ensure_image_object(browser_image*im)
{
    if(!im||!im->image||im->node_index>=g_doc.node_count)return -1;if(im->object)return 0;browser_node*n=&g_doc.nodes[im->node_index];if(!n->w||!n->h)return -1;
    for(uint32_t attempt=0u;attempt<80u;++attempt){uint32_t obj=0u;if(g_api->gfx_obj_add_image_from_img(im->image,n->x,n->y-g_scroll_y,&obj)==0){im->object=obj;(void)g_api->gfx_obj_set_parent(obj,g_viewport);(void)g_api->gfx_obj_set_clip_to_parent(obj,1u);(void)g_api->gfx_obj_set_z(obj,5);(void)g_api->gfx_obj_set_visible(obj,1u);(void)g_api->gfx_image_set_sample_mode(obj,SACX_GFX_IMAGE_SAMPLE_NEAREST);(void)g_api->gfx_image_set_pos(obj,n->x,n->y-g_scroll_y);(void)g_api->gfx_image_set_size(obj,n->w,n->h);if(g_api->log)g_api->log("Dihscover: image object ready");return 0;}if(!reclaim_image_graphics_slot())break;}
    if(g_api->log)g_api->log("Dihscover: image object allocation failed");return -1;
}

static void log_image_pixel_state(uint32_t image,uint32_t w,uint32_t h)
{
    if(!g_api->log||!SACX_API_HAS(g_api,img_pixels)||!w||!h)return;uint32_t*pixels=0u,stride=0u;if(g_api->img_pixels(image,&pixels,&stride)!=0||!pixels||stride<w)return;uint32_t opaque=0u,dark=0u,light=0u,varied=0u,first=0u;for(uint32_t gy=0u;gy<4u;++gy)for(uint32_t gx=0u;gx<4u;++gx){uint32_t px=pixels[((uint64_t)gy*(h-1u)/3u)*stride+((uint64_t)gx*(w-1u)/3u)];uint32_t a=px>>24,r=(px>>16)&255u,g=(px>>8)&255u,b=px&255u;if(a>16u)++opaque;if(r+g+b<48u)++dark;if(r+g+b>720u)++light;if((gx||gy)&&px!=first)++varied;else if(!gx&&!gy)first=px;}if(!opaque)g_api->log("Dihscover: decoded image samples transparent");else if(!varied&&light==16u)g_api->log("Dihscover: decoded image samples white");else if(!varied&&dark==16u)g_api->log("Dihscover: decoded image samples black");else g_api->log("Dihscover: decoded image samples contain colour");
}

static void pump_images(void)
{
    for(uint32_t i=0u;i<DIHSCOVER_IMAGE_CAP;++i) if(g_images[i].request) {
        uint32_t st=g_api->net_request_status(g_images[i].request);
        if(st==SACX_NET_STATUS_DONE) {
            sacx_net_response_info info; uint32_t got=0u;
            if(g_api->log)g_api->log("Dihscover: image decode begin");
            if(g_api->net_response_info(g_images[i].request,&info)==0 && info.body_size<DIHSCOVER_BODY_CAP &&
               g_api->net_response_read(g_images[i].request,0u,g_body,info.body_size,&got)==0 && got==info.body_size &&
               g_api->img_load_memory(g_body,got,&g_images[i].image)==0){browser_node*n=&g_doc.nodes[g_images[i].node_index];uint32_t iw=0u,ih=0u;if(g_api->img_size&&g_api->img_size(g_images[i].image,&iw,&ih)==0&&iw&&ih){log_image_pixel_state(g_images[i].image,iw,ih);uint32_t maxw=g_view_w>48u?g_view_w-48u:g_view_w;if(!n->style.width_px&&!n->style.height_px){n->style.width_px=(uint16_t)(iw>maxw?maxw:iw);n->style.height_px=(uint16_t)(((uint64_t)ih*n->style.width_px)/iw);}else if(n->style.width_px&&!n->style.height_px)n->style.height_px=(uint16_t)(((uint64_t)ih*n->style.width_px)/iw);else if(!n->style.width_px&&n->style.height_px)n->style.width_px=(uint16_t)(((uint64_t)iw*n->style.height_px)/ih);if(!n->style.width_px)n->style.width_px=1u;if(!n->style.height_px)n->style.height_px=1u;}g_images[i].failed=0u;relayout_preserving_scroll();(void)ensure_image_object(&g_images[i]);}else g_images[i].failed=1u;
            if(g_api->log)g_api->log("Dihscover: image decode done");(void)g_api->net_request_release(g_images[i].request); g_images[i].request=0u; g_images[i].loading=0u; clear_paint();
        } else if(st==SACX_NET_STATUS_FAILED||st==SACX_NET_STATUS_CANCELLED) { (void)g_api->net_request_release(g_images[i].request); g_images[i].request=0u;g_images[i].loading=0u;if(!g_images[i].reserved){g_images[i].reserved=1u;set_status("Retrying image");}else g_images[i].failed=1u; }
        return;
    }
    if(!start_next_image()) { g_load_state=LOAD_IDLE; set_status("Done"); }
}

static void paint_underline_line(uint32_t object,const browser_node*n,const char*line,uint32_t scale,int32_t text_x,int32_t y)
{
    if(!object)return;uint32_t width=g_api->text_measure_line_px?g_api->text_measure_line_px(line,scale,0):n->w;if(width>n->w)width=n->w;int32_t x=text_x;if(n->style.text_align==1u)x-=(int32_t)(width/2u);else if(n->style.text_align==2u)x-=(int32_t)width;(void)g_api->gfx_obj_set_rect(object,x,y,width,n->style.font_px>=24u?2u:1u);(void)g_api->gfx_obj_set_fill_rgb(object,n->style.color.r,n->style.color.g,n->style.color.b);(void)g_api->gfx_obj_set_visible(object,n->style.underline&&line[0]?1u:0u);
}

static void paint_document(void)
{
    uint32_t used=0u;
    for(uint32_t i=0u;i<g_paint_count;++i) g_paint[i].active=0u;
    for(uint32_t pass=0u;pass<2u&&used<g_paint_count;++pass) for(uint32_t i=1u;i<g_doc.node_count&&used<g_paint_count;++i) {
        browser_node *n=&g_doc.nodes[i]; int32_t sy=n->y-g_scroll_y;
        if(!n->h||sy+(int32_t)n->h<0||sy>(int32_t)g_view_h) continue;
        if(n->tag==B_TAG_IMG&&n->image_slot>=0) continue;
        if((int32_t)i==g_page_input_node) continue;
        if(!n->text_off&&!n->style.has_background&&!n->style.border_width) continue;
        if(pass==0u&&!n->text_off)continue;
        if(pass==1u&&n->text_off)continue;
        browser_paint *p=&g_paint[used++];
        p->node=(int32_t)i;p->active=1u;
        uint32_t scale=browser_text_scale(n->style.font_px);
        if(n->text_off){uint32_t ignored=0u;uint32_t inner=n->w>n->style.padding_left+n->style.padding_right?n->w-n->style.padding_left-n->style.padding_right:n->w;(void)browser_text_wrap(&n->style,browser_document_string(&g_doc,n->text_off),n->style.nowrap?0x100000u:inner,p->text,sizeof(p->text),&ignored);}else p->text[0]=0;
        int32_t text_x=n->x+(int32_t)n->style.padding_left;
        if(n->style.text_align==1u)text_x=n->x+(int32_t)(n->w/2u);else if(n->style.text_align==2u)text_x=n->x+(int32_t)n->w-(int32_t)n->style.padding_right;
        (void)g_api->gfx_text_set(p->object,p->text); (void)g_api->gfx_text_set_pos(p->object,text_x,sy+(int32_t)n->style.padding_top);
        (void)g_api->gfx_text_set_scale(p->object,scale);
        (void)g_api->gfx_text_set_align(p->object,n->style.text_align==1u?SACX_TEXT_ALIGN_CENTER:n->style.text_align==2u?SACX_TEXT_ALIGN_RIGHT:SACX_TEXT_ALIGN_LEFT);
        (void)g_api->gfx_obj_set_fill_rgb(p->object,n->style.color.r,n->style.color.g,n->style.color.b);
        (void)g_api->gfx_obj_set_visible(p->object,n->text_off?1u:0u);
        if(p->background_object){
            (void)g_api->gfx_obj_set_rect(p->background_object,n->x,sy,n->w,n->h);
            (void)g_api->gfx_obj_set_fill_rgb(p->background_object,n->style.background.r,n->style.background.g,n->style.background.b);
            (void)g_api->gfx_obj_set_outline_rgb(p->background_object,n->style.border_color.r,n->style.border_color.g,n->style.border_color.b);
            (void)g_api->gfx_obj_set_outline_width(p->background_object,n->style.border_width);
            (void)g_api->gfx_obj_set_outline_alpha(p->background_object,n->style.border_width?255u:0u);
            (void)g_api->gfx_obj_set_visible(p->background_object,(n->style.has_background||n->style.border_width)?1u:0u);
        }
        if(p->underline_object){
            uint32_t glyph_h=g_api->text_line_height?g_api->text_line_height(scale,0u):n->style.font_px;uint32_t line_h=n->style.line_height_px?n->style.line_height_px:glyph_h+4u;uint32_t underline_y=glyph_h>2u?glyph_h-2u:glyph_h;uint32_t handles[3]={p->underline_object,p->underline_object2,p->underline_object3};uint32_t at=0u;
            for(uint32_t line_index=0u;line_index<3u;++line_index){char line[256];uint32_t z=0u;while(p->text[at]&&p->text[at]!='\n'&&z+1u<sizeof(line))line[z++]=p->text[at++];line[z]=0;if(p->text[at]=='\n')++at;paint_underline_line(handles[line_index],n,line,scale,text_x,sy+(int32_t)n->style.padding_top+(int32_t)(line_index*line_h+underline_y));if(!p->text[at]){for(uint32_t k=line_index+1u;k<3u;++k)if(handles[k])(void)g_api->gfx_obj_set_visible(handles[k],0u);break;}}
        }
    }
    for(uint32_t i=used;i<g_paint_count;++i){if(g_paint[i].object)(void)g_api->gfx_obj_set_visible(g_paint[i].object,0u);if(g_paint[i].underline_object)(void)g_api->gfx_obj_set_visible(g_paint[i].underline_object,0u);if(g_paint[i].underline_object2)(void)g_api->gfx_obj_set_visible(g_paint[i].underline_object2,0u);if(g_paint[i].underline_object3)(void)g_api->gfx_obj_set_visible(g_paint[i].underline_object3,0u);if(g_paint[i].background_object)(void)g_api->gfx_obj_set_visible(g_paint[i].background_object,0u);}
    for(uint32_t i=0u;i<DIHSCOVER_IMAGE_CAP;++i) if(g_images[i].image) {
        browser_node *n=&g_doc.nodes[g_images[i].node_index];
        if(!g_images[i].object)(void)ensure_image_object(&g_images[i]);
        if(g_images[i].object){(void)g_api->gfx_image_set_pos(g_images[i].object,n->x,n->y-g_scroll_y);(void)g_api->gfx_image_set_size(g_images[i].object,n->w,n->h);}
    }
}

static void layout_ui(void)
{
    int32_t x,y; uint32_t w,h;
    if(g_api->gfx_obj_get_rect(g_root,&x,&y,&w,&h)!=0)return;
    g_root_x=x;g_root_y=y;g_root_w=w;g_root_h=h;g_view_w=w;g_view_h=h>UI_DOCUMENT_Y?h-UI_DOCUMENT_Y:1u;
    for(uint32_t i=0u;i<3u;++i){(void)g_api->gfx_obj_set_rect(g_buttons[i].root,8+(int32_t)i*38,UI_TOOLBAR_Y,32,30);(void)g_api->gfx_text_set_pos(g_buttons[i].label,16,0);}
    (void)g_api->gfx_obj_set_rect(g_buttons[3].root,(int32_t)w-52,UI_TOOLBAR_Y,44,30);
    (void)g_api->gfx_text_set_pos(g_buttons[3].label,22,0);
    (void)g_api->textbox_set_bounds(g_textbox,122,UI_TOOLBAR_Y,w>190u?w-190u:40u,30);
    (void)g_api->gfx_obj_set_rect(g_viewport,0,UI_DOCUMENT_Y,w,g_view_h);
    (void)g_api->gfx_obj_set_rect(g_page_bg,0,0,w,g_view_h);
    (void)g_api->gfx_text_set_pos(g_status_obj,10,UI_STATUS_Y); (void)g_api->gfx_text_set_pos(g_tls_obj,(int32_t)w-10,UI_STATUS_Y);
}

static void poll_toolbar_input(void)
{
    static const uint32_t actions[4]={ACTION_BACK,ACTION_FORWARD,ACTION_RELOAD,ACTION_GO};
    uint32_t pressed=0u;
    for(uint32_t i=0u;i<4u;++i)if(g_api->button_pressed(g_buttons[i].button))pressed|=1u<<i;
    uint32_t released=g_button_pressed_mask&~pressed;
    for(uint32_t i=0u;i<4u;++i)if(released&(1u<<i)){g_action=actions[i];break;}
    g_button_pressed_mask=pressed;uint8_t focused=(uint8_t)(g_api->textbox_focused(g_textbox)>0);
    if(focused&&!g_prev_address_focus&&SACX_API_HAS(g_api,textbox_select))(void)g_api->textbox_select(g_textbox,0u,DIHSCOVER_URL_CAP-1u);
    g_prev_address_focus=focused;
    if(focused&&
       (g_api->input_key_pressed(SACX_KEY_ENTER)||g_api->input_key_pressed(SACX_KEY_KP_ENTER)))
        g_action=ACTION_GO;
}

static void handle_action(void)
{
    uint32_t action=g_action; g_action=ACTION_NONE;
    if(action==ACTION_GO){char url[DIHSCOVER_URL_CAP];if(g_api->textbox_text_copy(g_textbox,url,sizeof(url))>0)navigate(url,1);else show_error("Invalid address","Enter a URL before pressing Go.");}
    else if(action==ACTION_RELOAD&&g_history_count)navigate(g_history[g_history_at].url,0);
    else if(action==ACTION_BACK&&g_history_at>0u){history_save_scroll();--g_history_at;int32_t saved=g_history[g_history_at].scroll_y;if(!restore_cached_page(g_history[g_history_at].url))navigate(g_history[g_history_at].url,0);g_scroll_y=saved;}
    else if(action==ACTION_FORWARD&&g_history_at+1u<g_history_count){history_save_scroll();++g_history_at;navigate(g_history[g_history_at].url,0);g_scroll_y=g_history[g_history_at].scroll_y;}
}

static int update(const sacx_api *api)
{
    sacx_mouse_state mouse; g_api=api;
    if(!api->window_visible(g_window))return api->app_exit(0,"Dihscover closed");
    layout_ui(); if(g_network_ready)pump_retired_requests(); poll_toolbar_input(); handle_action();
    if(g_load_state==LOAD_DOCUMENT&&g_request){uint32_t st=api->net_request_status(g_request);if(st==SACX_NET_STATUS_DONE)finish_document();else if(st==SACX_NET_STATUS_FAILED||st==SACX_NET_STATUS_CANCELLED){sacx_net_response_info info;b_memset(&info,0,sizeof(info));(void)api->net_response_info(g_request,&info);(void)api->net_request_release(g_request);g_request=0u;show_error("Could not load page",info.error[0]?info.error:"The network request failed.");}}
    else if(g_load_state==LOAD_STYLES)pump_stylesheets();
    else if(g_load_state==LOAD_IMAGES)pump_images();
    if(browser_scripts_pump(&g_doc,api->time_ticks(),64u)>0){browser_document_layout(&g_doc,g_view_w);clear_paint();}
    sync_page_input();
    if(api->mouse_get_state(&mouse)==0&&api->window_focused(g_window)){
        if(mouse.wheel){g_scroll_y-=mouse.wheel*42;if(g_scroll_y<0)g_scroll_y=0;int32_t max=(int32_t)g_doc.content_height-(int32_t)g_view_h;if(max<0)max=0;if(g_scroll_y>max)g_scroll_y=max;if(g_load_state==LOAD_IDLE)g_load_state=LOAD_IMAGES;}
        int32_t lx=mouse.x-g_root_x,ly=mouse.y-g_root_y;
        uint32_t cursor=SACX_MOUSE_CURSOR_ARROW;
        if(ly>=UI_TOOLBAR_Y&&ly<UI_TOOLBAR_Y+30&&lx>=122&&lx<(int32_t)g_root_w-68)cursor=SACX_MOUSE_CURSOR_BEAM;
        else if(ly>=UI_TOOLBAR_Y&&ly<UI_TOOLBAR_Y+30&&((lx>=8&&lx<116)||(lx>=(int32_t)g_root_w-52&&lx<(int32_t)g_root_w-8)))cursor=SACX_MOUSE_CURSOR_LINK;
        if(g_suppress_document_click&&!(mouse.buttons&1u))g_suppress_document_click=0u;
        if(ly>=UI_DOCUMENT_Y){
            int hit=browser_document_hit_test(&g_doc,lx,ly-UI_DOCUMENT_Y+g_scroll_y);
            if(hit>=0){browser_node*hover=&g_doc.nodes[hit];cursor=(hover->tag==B_TAG_INPUT&&!(hover->flags&(8u|16u)))?SACX_MOUSE_CURSOR_BEAM:SACX_MOUSE_CURSOR_LINK;}
            else if(g_load_state==LOAD_DOCUMENT||g_load_state==LOAD_STYLES)cursor=SACX_MOUSE_CURSOR_WAIT;
            (void)api->mouse_set_cursor(cursor);
            if(!g_suppress_document_click&&(mouse.buttons&1u)&&!(g_prev_mouse&1u)&&hit>=0){
                browser_node *n=&g_doc.nodes[hit];
                if(n->flags&32u){}
                else if(n->tag==B_TAG_INPUT&&!(n->flags&(8u|16u))){focus_page_input((uint32_t)hit);}
                else if(n->href_off){char href[DIHSCOVER_URL_CAP],url[DIHSCOVER_URL_CAP],direct[DIHSCOVER_URL_CAP];b_copy(href,sizeof(href),browser_document_string(&g_doc,n->href_off));int changed=browser_scripts_click(&g_doc,(uint32_t)hit);if(changed>0){browser_document_layout(&g_doc,g_view_w);clear_paint();}if(browser_url_resolve(g_doc.base_url,href,url,sizeof(url))==0&&browser_url_unwrap_navigation(url,direct,sizeof(direct))==0)navigate(direct,1);}
                else{
                    uint32_t form=0u;int scripted=browser_scripts_click(&g_doc,(uint32_t)hit);int activated=browser_document_activate(&g_doc,(uint32_t)hit,&form);
                    if(activated==1||scripted>0){browser_document_layout(&g_doc,g_view_w);clear_paint();}
                    if(activated==2)submit_form(form);
                }
            }
        }
        else (void)api->mouse_set_cursor(cursor);
        g_prev_mouse=mouse.buttons;
    }
    paint_document(); return api->app_yield();
}

static int create_ui(void)
{
    sacx_window_style ws=sacx_window_style_default(); sacx_textbox_style ts=sacx_textbox_style_default();
    ws.body_fill=rgb(245,247,248);ws.body_outline=rgb(55,66,74);ws.titlebar_fill=rgb(30,38,45);ws.title_color=rgb(245,248,250);ws.titlebar_height=UI_TITLEBAR_H;
    if(g_api->window_create_ex(90,56,860,620,28,"Dihscover",&ws,&g_window)!=0||g_api->window_root(g_window,&g_root)!=0)return -1;
    (void)g_api->window_set_visible(g_window,1u);
    if(g_api->log)g_api->log("Dihscover: window visible");
    if(make_button(&g_buttons[0],"<",ACTION_BACK)||make_button(&g_buttons[1],">",ACTION_FORWARD)||make_button(&g_buttons[2],"R",ACTION_RELOAD)||make_button(&g_buttons[3],"Go",ACTION_GO))return -1;
    ts.fill=rgb(255,255,255);ts.focus_fill=rgb(255,255,255);ts.outline=rgb(112,124,134);ts.focus_outline=rgb(21,137,166);ts.text_color=rgb(22,28,33);ts.text_scale=1u;
    if(g_api->textbox_add_rect(122,UI_TOOLBAR_Y,650,30,20,&ts,0,0,&g_textbox)!=0||g_api->textbox_root(g_textbox,&g_textbox_root)!=0)return -1;
    (void)g_api->gfx_obj_set_parent(g_textbox_root,g_root);(void)g_api->textbox_set_max_len(g_textbox,DIHSCOVER_URL_CAP-1u);
    if(g_api->gfx_obj_add_rect(0,UI_DOCUMENT_Y,860,514,1,rgb(255,255,255),1,&g_viewport)!=0||g_api->gfx_obj_add_rect(0,0,860,514,0,rgb(255,255,255),1,&g_page_bg)!=0)return -1;
    (void)g_api->gfx_obj_set_parent(g_viewport,g_root);(void)g_api->gfx_obj_set_parent(g_page_bg,g_viewport);(void)g_api->gfx_obj_set_clip_to_parent(g_page_bg,1u);
    ts.fill=rgb(255,255,255);ts.focus_fill=rgb(255,255,255);ts.outline=rgb(105,115,124);ts.focus_outline=rgb(15,126,154);ts.text_color=rgb(28,34,40);ts.text_scale=1u;ts.padding_x=7u;ts.padding_y=5u;
    if(g_api->textbox_add_rect(0,0,220,34,18,&ts,page_textbox_submit,0,&g_page_textbox)!=0||g_api->textbox_root(g_page_textbox,&g_page_textbox_root)!=0)return -1;
    (void)g_api->gfx_obj_set_parent(g_page_textbox_root,g_viewport);(void)g_api->gfx_obj_set_clip_to_parent(g_page_textbox_root,1u);(void)g_api->gfx_obj_set_visible(g_page_textbox_root,0u);(void)g_api->textbox_set_max_len(g_page_textbox,511u);
    if(g_api->gfx_obj_add_text("Ready",10,UI_STATUS_Y,20,rgb(73,84,92),255,1,0,0,SACX_TEXT_ALIGN_LEFT,1,&g_status_obj)!=0||g_api->gfx_obj_add_text("",850,UI_STATUS_Y,20,rgb(172,91,22),255,1,0,0,SACX_TEXT_ALIGN_RIGHT,1,&g_tls_obj)!=0)return -1;
    (void)g_api->gfx_obj_set_parent(g_status_obj,g_root);(void)g_api->gfx_obj_set_parent(g_tls_obj,g_root);
    /* Reserve text capacity first. Decorations are optional when the global graphics pool is busy. */
    for(uint32_t i=0u;i<DIHSCOVER_PAINT_CAP;++i){if(g_api->gfx_obj_add_text("",0,0,4,rgb(28,34,40),255,1,0,2,SACX_TEXT_ALIGN_LEFT,0,&g_paint[i].object)!=0)break;(void)g_api->gfx_obj_set_parent(g_paint[i].object,g_viewport);(void)g_api->gfx_obj_set_clip_to_parent(g_paint[i].object,1u);g_paint[i].node=-1;g_paint_count=i+1u;}
    if(g_paint_count<DIHSCOVER_PAINT_CAP&&g_api->log)g_api->log("Dihscover: graphics pool limited text virtualization");
    for(uint32_t i=0u;i<g_paint_count&&i<32u;++i)if(g_api->gfx_obj_add_rect(0,0,1,1,2,rgb(255,255,255),0,&g_paint[i].background_object)==0){(void)g_api->gfx_obj_set_parent(g_paint[i].background_object,g_viewport);(void)g_api->gfx_obj_set_clip_to_parent(g_paint[i].background_object,1u);}
    for(uint32_t i=0u;i<g_paint_count&&i<24u;++i)if(g_api->gfx_obj_add_rect(0,0,1,1,3,rgb(16,92,172),0,&g_paint[i].underline_object)==0){(void)g_api->gfx_obj_set_parent(g_paint[i].underline_object,g_viewport);(void)g_api->gfx_obj_set_clip_to_parent(g_paint[i].underline_object,1u);}
    for(uint32_t i=0u;i<g_paint_count&&i<4u;++i){if(g_api->gfx_obj_add_rect(0,0,1,1,3,rgb(16,92,172),0,&g_paint[i].underline_object2)==0){(void)g_api->gfx_obj_set_parent(g_paint[i].underline_object2,g_viewport);(void)g_api->gfx_obj_set_clip_to_parent(g_paint[i].underline_object2,1u);}if(g_api->gfx_obj_add_rect(0,0,1,1,3,rgb(16,92,172),0,&g_paint[i].underline_object3)==0){(void)g_api->gfx_obj_set_parent(g_paint[i].underline_object3,g_viewport);(void)g_api->gfx_obj_set_clip_to_parent(g_paint[i].underline_object3,1u);}}
    layout_ui();(void)g_api->textbox_set_focus(g_textbox,1u);return 0;
}

extern "C" int sacx_main(const sacx_api *api)
{
    if(!api||api->abi_version!=SACX_API_ABI_VERSION)return -1;
    g_api=api;(void)SACX_APP_NO_CONSOLE(api);browser_heap_set_api(api);browser_document_set_api(api);
    if(browser_heap_init()!=0||browser_document_init(&g_doc)!=0)return api->app_exit(-1,"Dihscover heap allocation failed");
    browser_document_reset(&g_doc,g_generation,"");
    g_network_ready=(uint8_t)(SACX_API_HAS(api,net_request_start)&&SACX_API_HAS(api,img_load_memory));
    if(api->log)api->log("Dihscover: starting UI");
    if(create_ui()!=0)return api->app_exit(-1,"Dihscover UI failed");
    if(!g_network_ready)show_error("Browser API unavailable","This kernel does not export the Dihscover network API.");
    else set_status("Ready");
    if(api->app_set_update(update)!=0)return api->app_exit(-1,"Dihscover update failed");
    return 0;
}
