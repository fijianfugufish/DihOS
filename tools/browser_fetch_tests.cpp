#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../sdk/sacx/apps/dihscover/netsurf_fetch.cpp"
struct nsurl {std::string text;unsigned refs;};
struct fetch {dihos_fetch *context;long code;};
extern "C" {lwc_string *corestring_lwc_http,*corestring_lwc_https;}
static sacx_api api;static std::string header,body,received,redirect;static std::vector<std::string> headers;
static unsigned releases,cancels,finished,errors,redirects,requests;static bool abort_on_data;
static uint32_t response_code=200;static fetch *owner;
static uint32_t response_status=SACX_NET_STATUS_LOADING;
static std::string failure_detail,received_error;
uint32_t dihs_strlen(const char *s){return (uint32_t)strlen(s);}
void *dihs_memcpy(void *out,const void *src,__SIZE_TYPE__ bytes){return memcpy(out,src,bytes);}
void dihs_copy(char *out,uint32_t cap,const char *src){snprintf(out,cap,"%s",src);}
extern "C" const sacx_api *dihscover_netsurf_api(){return &api;}
extern "C" nserror nsurl_create(const char *text,nsurl **out){*out=new nsurl{text,1};return NSERROR_OK;}
extern "C" nsurl *nsurl_ref(nsurl *url){++url->refs;return url;}
extern "C" void nsurl_unref(nsurl *url){if(!--url->refs)delete url;}
extern "C" const char *nsurl_access(const nsurl *url){return url->text.c_str();}
extern "C" nserror fetcher_add(lwc_string*,const fetcher_operation_table*){return NSERROR_OK;}
extern "C" void fetch_set_http_code(fetch *parent,long code){parent->code=code;}
extern "C" void fetch_remove_from_queues(fetch*){}
extern "C" void fetch_free(fetch *parent){assert(parent->context);fetch_free_context(parent->context);parent->context=nullptr;}
extern "C" void fetch_send_callback(const fetch_msg *msg,fetch *parent){
 assert(parent==owner&&parent->context);
 if(msg->type==FETCH_HEADER)headers.emplace_back((const char*)msg->data.header_or_data.buf,msg->data.header_or_data.len);
 if(msg->type==FETCH_DATA){assert(msg->data.header_or_data.len<=4096);received.append((const char*)msg->data.header_or_data.buf,msg->data.header_or_data.len);if(abort_on_data)fetch_abort(parent->context);}
 if(msg->type==FETCH_FINISHED)++finished;
 if(msg->type==FETCH_ERROR){++errors;received_error=msg->data.error;}
 if(msg->type==FETCH_REDIRECT){++redirects;redirect=msg->data.redirect;fetch_abort(parent->context);}
}
static int start(const sacx_net_request_desc_ex *desc,uint32_t *id){assert(desc->url&&desc->method);++requests;*id=1;return 0;}
static int cancel(uint32_t){++cancels;return 0;}
static int release(uint32_t){++releases;return 0;}
static uint32_t status(uint32_t){return response_status;}
static int info(uint32_t,sacx_net_response_info *out){*out={};out->http_status=response_code;out->body_size=(uint32_t)body.size();snprintf(out->error,sizeof out->error,"%s",failure_detail.c_str());return 0;}
static int progress(uint32_t,sacx_net_progress *out){*out={SACX_NET_STATUS_LOADING,1,(uint32_t)body.size(),(uint32_t)body.size(),1,0};return 0;}
static int read_headers(uint32_t,uint32_t offset,void *out,uint32_t cap,uint32_t *got){*got=(uint32_t)header.size()-offset;if(*got>cap)*got=cap;memcpy(out,header.data()+offset,*got);return 0;}
static int read_body(uint32_t,uint32_t offset,void *out,uint32_t cap,uint32_t *got){*got=(uint32_t)body.size()-offset;if(*got>cap)*got=cap;memcpy(out,body.data()+offset,*got);return 0;}
static void setup(fetch &parent,const char *url="https://example.test/old"){
 received.clear();headers.clear();redirect.clear();received_error.clear();failure_detail.clear();response_status=SACX_NET_STATUS_LOADING;releases=cancels=finished=errors=redirects=requests=0;abort_on_data=false;owner=&parent;
 nsurl *target;assert(nsurl_create(url,&target)==NSERROR_OK);parent.context=(dihos_fetch*)fetch_setup(&parent,target,false,false,nullptr,nullptr,nullptr);nsurl_unref(target);
 assert(fetch_start(parent.context));assert(headers.empty()&&!finished&&!errors&&!redirects);
}
int main(){
 api.struct_size=sizeof api;api.net_request_start_stream=start;api.net_request_status=status;api.net_request_cancel=cancel;api.net_request_release=release;api.net_response_info=info;api.net_response_progress=progress;api.net_response_headers_read=read_headers;api.net_response_read_available=read_body;
 header="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: max-age=60\r\nETag: test\r\n\r\n";body=std::string(33000,'x');fetch parent={};setup(parent);
 for(unsigned i=0;i<20&&parent.context;++i)fetch_poll_scheme(nullptr);
 assert(!parent.context&&received==body&&finished==1&&!errors&&releases==1);
 bool cache=false,charset=false;for(auto &h:headers){cache|=h=="Cache-Control: max-age=60";charset|=h=="Content-Type: text/html; charset=utf-8";}assert(cache&&charset);
 setup(parent);abort_on_data=true;fetch_poll_scheme(nullptr);assert(!parent.context&&received.size()==4096&&!finished&&!errors&&cancels==1&&releases==1);
 response_code=307;header="HTTP/1.1 307 Temporary Redirect\r\nLocation: /new\r\nContent-Length: 0\r\n\r\n";body.clear();setup(parent);fetch_poll_scheme(nullptr);assert(!parent.context&&parent.code==307&&redirect=="/new"&&redirects==1&&!finished&&releases==1);
 setup(parent,"https://duckduckgo.com/l/?uddg=https%3A%2F%2Fexample.test%2Ftarget");assert(!requests);fetch_poll_scheme(nullptr);assert(!parent.context&&redirect=="https://example.test/target"&&redirects==1&&!releases);
 setup(parent);response_status=SACX_NET_STATUS_FAILED;failure_detail="TLS handshake failed, engine error 54";fetch_poll_scheme(nullptr);assert(!parent.context&&errors==1&&received_error==failure_detail&&releases==1);
 setup(parent);response_status=SACX_NET_STATUS_CANCELLED;fetch_poll_scheme(nullptr);assert(!parent.context&&errors==1&&received_error=="Network request cancelled"&&releases==1);
 assert(!g_fetches);puts("PASS: streamed fetch, headers, cancellation, redirects and kernel failure details including TLS engine errors");
}
