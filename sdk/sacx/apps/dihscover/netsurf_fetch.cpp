#include "dihscover.h"
#include <stdlib.h>

extern "C" {
#include <libwapcaplet/libwapcaplet.h>
#include "content/fetch.h"
#include "content/fetchers.h"
#include "utils/corestrings.h"
#include "utils/nsurl.h"
}

extern "C" const sacx_api *dihscover_netsurf_api(void);

typedef struct dihos_fetch {
    dihos_fetch *next;
    struct fetch *parent;
    nsurl *url;
    uint32_t request;
    uint32_t body_size;
    uint32_t body_offset;
    uint8_t aborted;
    uint8_t locked;
    uint8_t response_started;
    uint8_t terminal;
    char *post_data;
} dihos_fetch;

static dihos_fetch *g_fetches;
static uint32_t g_completed_fetches;
static uint32_t g_failed_fetches;

static void log_response(const sacx_net_response_info *info)
{
    const sacx_api *api=dihscover_netsurf_api();if(!api||!api->log||!info)return;char text[176]="Dihscover NetSurf response: status=";uint32_t at=35u;char tmp[12];uint32_t n=0u,value=info->http_status;
    if(!value)tmp[n++]='0';while(value&&n<sizeof(tmp)){tmp[n++]=(char)('0'+value%10u);value/=10u;}while(n&&at+1u<sizeof(text))text[at++]=tmp[--n];
    const char body[]=" body=";for(uint32_t i=0;body[i]&&at+1u<sizeof(text);++i)text[at++]=body[i];n=0u;value=info->body_size;if(!value)tmp[n++]='0';while(value&&n<sizeof(tmp)){tmp[n++]=(char)('0'+value%10u);value/=10u;}while(n&&at+1u<sizeof(text))text[at++]=tmp[--n];
    const char mime[]=" mime=";for(uint32_t i=0;mime[i]&&at+1u<sizeof(text);++i)text[at++]=mime[i];for(uint32_t i=0;info->content_type[i]&&at+1u<sizeof(text);++i)text[at++]=info->content_type[i];text[at]=0;api->log(text);
}

static char *copy_string(const char *text)
{
    uint32_t size=dihs_strlen(text)+1u;char *copy=(char*)malloc(size);
    if(copy)dihs_memcpy(copy,text,size);return copy;
}

static void send_message(dihos_fetch *ctx,const fetch_msg *message)
{
    ctx->locked=1u;fetch_send_callback(message,ctx->parent);ctx->locked=0u;
}

static void send_header(dihos_fetch *ctx,const char *name,const char *value)
{
    char line[256];uint32_t at=0u;
    while(name[at]&&at+1u<sizeof(line)){line[at]=name[at];++at;}
    if(at+2u<sizeof(line)){line[at++]=':';line[at++]=' ';}
    for(uint32_t i=0u;value&&value[i]&&at+1u<sizeof(line);++i)line[at++]=value[i];
    line[at]=0;fetch_msg msg={};msg.type=FETCH_HEADER;msg.data.header_or_data.buf=(const uint8_t*)line;msg.data.header_or_data.len=at;send_message(ctx,&msg);
}

static void send_content_length(dihos_fetch *ctx,uint32_t size)
{
    char value[16];uint32_t digits=0u;char reversed[12];
    if(!size)reversed[digits++]='0';
    while(size&&digits<sizeof(reversed)){reversed[digits++]=(char)('0'+(size%10u));size/=10u;}
    for(uint32_t i=0u;i<digits;++i)value[i]=reversed[digits-1u-i];value[digits]=0;
    send_header(ctx,"Content-Length",value);
}

static bool fetch_initialise(lwc_string *scheme){(void)scheme;return true;}
static void fetch_finalise(lwc_string *scheme){(void)scheme;}
static bool fetch_acceptable(const nsurl *url){(void)url;return true;}

static int hex_value(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static uint8_t duckduckgo_target(const char *url,char *out,uint32_t cap)
{
    const char *needle="duckduckgo.com/l/?uddg=";const char *at=url;uint32_t n=0u;
    while(*at){const char *p=at,*q=needle;while(*p&&*q&&*p==*q){++p;++q;}if(!*q){at=p;break;}++at;}if(!*at)return 0u;
    while(*at&&*at!='&'&&n+1u<cap){if(*at=='%'&&at[1]&&at[2]){int hi=hex_value(at[1]),lo=hex_value(at[2]);if(hi>=0&&lo>=0){out[n++]=(char)((hi<<4)|lo);at+=3;continue;}}out[n++]=*at=='+'?' ':*at;++at;}
    out[n]=0;return n>8u&&((out[0]=='h'&&out[1]=='t'&&out[2]=='t'&&out[3]=='p'))?1u:0u;
}

static void *fetch_setup(struct fetch *parent,nsurl *url,bool only_2xx,bool downgrade_tls,
                         const char *post_urlenc,const struct fetch_multipart_data *post_multipart,
                         const char **headers)
{
    (void)only_2xx;(void)downgrade_tls;(void)post_multipart;(void)headers;
    dihos_fetch *ctx=(dihos_fetch*)calloc(1,sizeof(*ctx));if(!ctx)return 0;
    ctx->parent=parent;ctx->url=nsurl_ref(url);ctx->post_data=post_urlenc?copy_string(post_urlenc):0;
    ctx->next=g_fetches;g_fetches=ctx;return ctx;
}

static bool fetch_start(void *opaque)
{
    dihos_fetch *ctx=(dihos_fetch*)opaque;const sacx_api *api=dihscover_netsurf_api();
    if(!api||!SACX_API_HAS(api,net_request_start))return false;
    /* DuckDuckGo result links are tracking wrappers that this tiny network
       stack receives as a 200-byte landing page rather than an HTTP redirect.
       Unwrap the destination before the request reaches the network. */
    {char target[DIHSCOVER_URL_CAP];if(duckduckgo_target(nsurl_access(ctx->url),target,sizeof(target))){
        /* Do not emit FETCH_REDIRECT synchronously here: core redirect handling
           can free this fetch context during its own callback.  Repoint this
           request to the destination instead, which is equivalent for a GET
           and avoids a use-after-free data abort. */
        nsurl *destination=0;if(nsurl_create(target,&destination)==NSERROR_OK){nsurl_unref(ctx->url);ctx->url=destination;}
    }}
    if(ctx->post_data&&SACX_API_HAS(api,net_request_start_ex)){
        sacx_net_request_desc_ex desc={};desc.url=nsurl_access(ctx->url);desc.max_response_bytes=8u*1024u*1024u;
        desc.timeout_ms=45000u;desc.redirect_limit=8u;desc.method="POST";desc.body=ctx->post_data;
        desc.body_size=dihs_strlen(ctx->post_data);desc.content_type="application/x-www-form-urlencoded";
        return api->net_request_start_ex(&desc,&ctx->request)==0;
    }
    sacx_net_request_desc desc={};desc.url=nsurl_access(ctx->url);desc.max_response_bytes=8u*1024u*1024u;
    desc.timeout_ms=45000u;desc.redirect_limit=8u;return api->net_request_start(&desc,&ctx->request)==0;
}

static void fetch_abort(void *opaque)
{
    dihos_fetch *ctx=(dihos_fetch*)opaque;ctx->aborted=1u;const sacx_api *api=dihscover_netsurf_api();if(api&&api->log)api->log("Dihscover NetSurf fetch: aborted by core before finished");
    if(ctx->request&&api)(void)api->net_request_cancel(ctx->request);
}

static void unlink_fetch(dihos_fetch *ctx)
{
    dihos_fetch **link=&g_fetches;while(*link&&*link!=ctx)link=&(*link)->next;if(*link)*link=ctx->next;
}

static void fetch_free_context(void *opaque)
{
    dihos_fetch *ctx=(dihos_fetch*)opaque;const sacx_api *api=dihscover_netsurf_api();unlink_fetch(ctx);
    if(ctx->request&&api)(void)api->net_request_release(ctx->request);if(ctx->url)nsurl_unref(ctx->url);
    free(ctx->post_data);free(ctx);
}

static void fail_fetch(dihos_fetch *ctx,const char *error)
{
    const sacx_api *api=dihscover_netsurf_api();if(api&&api->log){char text[192]="Dihscover NetSurf fetch error: ";dihs_copy(text+30u,sizeof(text)-30u,error?error:"unknown");api->log(text);}fetch_msg msg={};msg.type=FETCH_ERROR;msg.data.error=error;send_message(ctx,&msg);ctx->aborted=1u;ctx->terminal=1u;++g_failed_fetches;
    if(api&&SACX_API_HAS(api,app_set_console_visible))SACX_APP_SHOW_CONSOLE(api);
}

static void begin_response(dihos_fetch *ctx)
{
    const sacx_api *api=dihscover_netsurf_api();sacx_net_response_info info={};
    if(!api||api->net_response_info(ctx->request,&info)!=0||info.truncated){fail_fetch(ctx,"DihOS network response failed");return;}
    log_response(&info);
    uint32_t status=info.http_status?info.http_status:200u;
    fetch_set_http_code(ctx->parent,status);
    /* FETCH_HEADER messages are individual fields, not raw HTTP lines.  The
       old framing could leave NetSurf with a completed but untyped document. */
    send_header(ctx,"Content-Type",info.content_type[0]?info.content_type:"text/html; charset=utf-8");
    send_content_length(ctx,info.body_size);
    ctx->body_size=info.body_size;ctx->body_offset=0u;ctx->response_started=1u;
}

static void stream_response(dihos_fetch *ctx)
{
    const sacx_api *api=dihscover_netsurf_api();fetch_msg msg={};uint8_t chunk[4096];
    /* A callback can trigger parsing and a complete document reflow.  Feed
       only one bounded fragment per application update so network-heavy pages
       cannot monopolise the kernel and look frozen. */
    if(!ctx->aborted&&ctx->body_offset<ctx->body_size){uint32_t want=ctx->body_size-ctx->body_offset;if(want>sizeof(chunk))want=sizeof(chunk);uint32_t got=0u;
        if(api->net_response_read(ctx->request,ctx->body_offset,chunk,want,&got)!=0||!got){
            fail_fetch(ctx,"DihOS network response ended before its declared length");return;
        }
        msg.type=FETCH_DATA;msg.data.header_or_data.buf=chunk;msg.data.header_or_data.len=got;send_message(ctx,&msg);ctx->body_offset+=got;if(ctx->aborted){const sacx_api *log_api=dihscover_netsurf_api();if(log_api&&log_api->log)log_api->log("Dihscover NetSurf fetch: abort followed page-data callback");return;}
    }
    if(!ctx->aborted&&ctx->body_offset==ctx->body_size){msg.type=FETCH_FINISHED;send_message(ctx,&msg);ctx->terminal=1u;++g_completed_fetches;const sacx_api *log_api=dihscover_netsurf_api();if(log_api&&log_api->log)log_api->log("Dihscover NetSurf fetch: all response bytes delivered");}
}

static void fetch_poll_scheme(lwc_string *scheme)
{
    (void)scheme;const sacx_api *api=dihscover_netsurf_api();dihos_fetch *ctx=g_fetches;
    while(ctx){dihos_fetch *next=ctx->next;if(!ctx->locked&&ctx->request){uint32_t status=api->net_request_status(ctx->request);
        if(status==SACX_NET_STATUS_DONE){if(!ctx->response_started)begin_response(ctx);if(!ctx->terminal&&!ctx->locked)stream_response(ctx);}
        else if(status==SACX_NET_STATUS_FAILED||status==SACX_NET_STATUS_CANCELLED)fail_fetch(ctx,"DihOS network request failed");
        if(ctx->terminal){fetch_remove_from_queues(ctx->parent);fetch_free(ctx->parent);}
    }ctx=next;}
}

extern "C" uint32_t dihscover_fetch_active_count(void)
{
    uint32_t count=0u;for(dihos_fetch *ctx=g_fetches;ctx;ctx=ctx->next)if(ctx->request&&!ctx->aborted&&!ctx->terminal)++count;return count;
}

extern "C" uint32_t dihscover_fetch_completed_count(void){return g_completed_fetches;}
extern "C" uint32_t dihscover_fetch_failed_count(void){return g_failed_fetches;}

extern "C" nserror fetch_dihos_register(void)
{
    static const fetcher_operation_table operations={fetch_initialise,fetch_acceptable,fetch_setup,fetch_start,fetch_abort,fetch_free_context,fetch_poll_scheme,0,fetch_finalise};
    nserror result=fetcher_add(lwc_string_ref(corestring_lwc_http),&operations);if(result!=NSERROR_OK)return result;
    return fetcher_add(lwc_string_ref(corestring_lwc_https),&operations);
}
