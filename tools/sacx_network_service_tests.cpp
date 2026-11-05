#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
extern "C" {
#include "system/kfiber.h"
#include "apps/sacx_network_internal.h"
}
#define SACX_MAX_TASKS 2u
#define SACX_MAX_TASK_NET_REQUESTS 4u
#define SACX_NET_DEFAULT_BYTES 65536u
#define SACX_NET_MAX_BYTES (8u*1024u*1024u)
struct fake_task { sacx_net_request_slot net_requests[SACX_MAX_TASK_NET_REQUESTS]; };
static fake_task G_tasks[SACX_MAX_TASKS],*G_current_task;
static sacx_net_request_slot *G_net_active_slot;
extern "C" {
#include "process/dihos_user_network.h"
}
static unsigned char user_memory[8192];
static unsigned char large_user_memory[65536];
static unsigned char* mapped(uint64_t at,uint64_t n){
 if(at>=0x1000 && n<=sizeof(user_memory) && at-0x1000<=sizeof(user_memory)-n)return user_memory+at-0x1000;
 if(at>=0x10000 && n<=sizeof(large_user_memory) && at-0x10000<=sizeof(large_user_memory)-n)return large_user_memory+at-0x10000;
 return nullptr;
}
extern "C" int aarch64_user_vm_check_buffer(const aarch64_user_vm*,uint64_t at,uint64_t n,int){return mapped(at,n)?0:-14;}
extern "C" int aarch64_user_vm_copy_in(const aarch64_user_vm *vm,void *out,uint64_t at,uint64_t n){int rc=aarch64_user_vm_check_buffer(vm,at,n,0);if(!rc)memcpy(out,mapped(at,n),n);return rc;}
extern "C" int aarch64_user_vm_copy_out(const aarch64_user_vm *vm,uint64_t at,const void *in,uint64_t n){int rc=aarch64_user_vm_check_buffer(vm,at,n,1);if(!rc)memcpy(mapped(at,n),in,n);return rc;}
static uint64_t now;
static unsigned starts,polls,quiet;
static unsigned background_calls,background_steps;
static bool fail_pages;
static void (*checkpoint)(void);
static uint64_t dihos_time_monotonic_ms() {return now;}
static uint32_t smp_current_logical_id() {return 0;}
static void *pmem_alloc_pages(uint64_t n) {return fail_pages?nullptr:malloc((size_t)n*4096);}
static void pmem_free_pages(void *p,uint64_t) {free(p);}
static void asm_dma_clean_range(const void*,uint64_t) {}
#include "../kernel/apps/sacx_user_network.inc"
static int kwork_cancel(uint32_t) {return 0;}
static sacx_net_request_slot *sacx_net_slot_from_id(fake_task *task,uint32_t id) {return task&&id&&id<=4&&task->net_requests[id-1].used?&task->net_requests[id-1]:nullptr;}
static void sacx_copy_trunc(char *out,uint32_t cap,const char *in) {snprintf(out,cap,"%s",in);}
static void knet_link_set_checkpoint(void (*fn)(void)) {checkpoint=fn;}
static void knet_usb_set_worker_quiet(unsigned value) {quiet=value;}
static void knet_usb_background_poll()
{
    ++background_calls;
    for(unsigned i=0;i<7;++i){++now;++background_steps;checkpoint();assert(quiet);}
}
static void sacx_net_request_worker(void *opaque)
{
    auto *slot=(sacx_net_request_slot*)opaque;++starts;slot->status=SACX_NET_STATUS_LOADING;
    volatile double retained=3.25;
    for (unsigned i=0;i<1000;++i) {
        if(slot->cancelled){slot->status=SACX_NET_STATUS_CANCELLED;return;}
        ++now;++polls;retained+=0.125;checkpoint();
        assert(retained==3.25+(i+1)*0.125 && quiet);
    }
    slot->status=SACX_NET_STATUS_DONE;
}
#include "../kernel/apps/sacx_network_service.inc"
#include "../kernel/apps/sacx_network_start.inc"
#include "../kernel/apps/sacx_network_lifetime.inc"
int main()
{
    G_current_task=&G_tasks[0];
    char url[]="https://example.com";
    sacx_net_request_desc_ex desc={};desc.url=url;desc.method="POST";
    char body[]="owned";desc.body=body;desc.body_size=5;
    uint32_t id;
    char too_long[SACX_HTTP_URL_MAX+2];memset(too_long,'x',SACX_HTTP_URL_MAX+1);too_long[SACX_HTTP_URL_MAX+1]=0;desc.url=too_long;
    assert(sacx_api_net_request_start_ex(&desc,&id)==-1&&!id);desc.url=url;
    for(unsigned i=0;i<4;++i) assert(!sacx_api_net_request_start_ex(&desc,&id) && id==i+1);
    assert(!starts && !polls && !now); /* start cannot execute the worker */
    assert(sacx_api_net_request_start_ex(&desc,&id)==-3);
    url[8]='X';body[0]='X';
    assert(!strcmp(G_tasks[0].net_requests[0].url,"https://example.com"));
    assert(G_tasks[0].net_requests[0].request_body[0]=='o');
    sacx_runtime_network_poll();assert(!polls); /* no app callback reentry */
    G_current_task=nullptr;
    for(unsigned i=0;i<10;++i) {
        uint64_t before=now;sacx_runtime_network_poll();
        assert(now-before<=2 && !checkpoint && !quiet && G_net_active_slot);
    }
    assert(starts==1 && G_tasks[0].net_requests[0].status==SACX_NET_STATUS_LOADING);
    G_current_task=&G_tasks[0];assert(!sacx_api_net_request_cancel(1));G_current_task=nullptr;
    sacx_runtime_network_poll();assert(!G_net_active_slot);
    assert(G_tasks[0].net_requests[0].status==SACX_NET_STATUS_CANCELLED);
    for(unsigned i=0;i<1600;++i) sacx_runtime_network_poll();
    assert(starts==4 && !G_net_active_slot);
    for(unsigned i=1;i<4;++i) assert(G_tasks[0].net_requests[i].status==SACX_NET_STATUS_DONE);
    for(unsigned i=1;i<=4;++i){G_current_task=&G_tasks[0];assert(!sacx_api_net_request_release(i));}
    assert(!sacx_api_net_request_start_ex(&desc,&id));G_current_task=nullptr;sacx_runtime_network_poll();assert(G_net_active_slot);
    G_current_task=&G_tasks[0];assert(!sacx_api_net_request_release(id));
    assert(G_net_active_slot->buffer&&G_net_active_slot->cancelled&&G_net_active_slot->release_when_done);
    G_current_task=nullptr;sacx_runtime_network_poll();assert(!G_net_active_slot&&!G_tasks[0].net_requests[id-1].used); /* freed only after worker stopped */
    G_current_task=&G_tasks[0];assert(!sacx_api_net_request_start_ex(&desc,&id));assert(!sacx_api_net_request_cancel(id));assert(!sacx_api_net_request_release(id));
    G_current_task=nullptr;unsigned before=starts;sacx_runtime_network_poll();assert(starts==before); /* queued cancel never starts */
    G_current_task=&G_tasks[0];assert(!sacx_api_net_request_start_stream(&desc,&id));assert(G_tasks[0].net_requests[id-1].no_follow);assert(!sacx_api_net_request_release(id));G_current_task=nullptr;
    /* DHCP runs and suspends without any remote worker or application request. */
    while(G_net_background_active)sacx_runtime_network_poll();
    now+=100;unsigned previous=background_calls;
    for(unsigned i=0;i<6;++i){uint64_t began=now;sacx_runtime_network_poll();assert(now-began<=2&&!quiet&&!checkpoint);}
    assert(background_calls==previous+1&&background_steps>=7&&!G_net_background_active);
    // Native starts own URL bytes, never run a worker inline, and share the
    // same continuation without borrowing a legacy application identity.
    memcpy(user_memory,"http://example.com/",20);
    uint64_t a[3]={0x1000,19,1000};unsigned prior=starts;
    int64_t native=dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_START,a);
    assert(native>0 && starts==prior);user_memory[0]='X';
    auto *native_slot=user_http_find(0x101,native);assert(native_slot && native_slot->request.url[0]=='h' && !native_slot->request.no_follow);
    uint64_t header_args[3]={(uint64_t)native,0x1100,64};
    assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,header_args)==-95);
    a[0]=native;a[1]=0x1100;a[2]=sizeof(sacx_user_http_info);
    assert(dihos_user_network_call(nullptr,0x102,SACX_USER_HTTP_POLL,a)==-9);
    assert(!dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_POLL,a));
    a[1]=0;a[2]=4;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_READ,a)==-14);
    a[1]=0x1100;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_READ,a)==-11);
    for(unsigned i=0;i<20 && G_net_active_slot!=&native_slot->request;++i)sacx_runtime_network_poll();
    assert(G_net_active_slot==&native_slot->request);
    auto *pinned=G_net_active_slot->buffer;
    dihos_user_network_cleanup(0x101);assert(!native_slot->owner && G_net_active_slot->buffer==pinned && G_net_active_slot->release_when_done);
    sacx_runtime_network_poll();assert(!native_slot->request.used && !G_net_active_slot);
    a[0]=native;a[1]=0;a[2]=0;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_CLOSE,a)==-9);
    memcpy(user_memory,"https://example.com/",21);a[0]=0x1000;a[1]=20;a[2]=1000;
    int64_t next=dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_START,a);assert(next>0 && next!=native);
    native_slot=user_http_find(0x101,next);
    a[0]=next;a[1]=0x1100;a[2]=sizeof(sacx_user_http_info);assert(!dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_POLL,a));
    sacx_user_http_info pending;memcpy(&pending,user_memory+0x100,sizeof(pending));assert(!pending.http_status && !pending.available_bytes && !pending.tls_unverified);
    memcpy(native_slot->request.buffer,"hello",5);native_slot->request.body_size=5;
    a[0]=next;a[1]=0x1100;a[2]=sizeof(sacx_user_http_info);assert(!dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_POLL,a));
    sacx_user_http_info info;memcpy(&info,user_memory+0x100,sizeof(info));assert(info.tls_unverified && info.available_bytes==5);
    a[2]=4;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_READ,a)==-13 && !native_slot->consumed);
    const char tls_head[]="HTTP/1.1 200 OK\r\n\r\n";
    native_slot->request.no_follow=1;native_slot->request.stream.headers_ready=1;
    native_slot->request.stream.header_bytes=sizeof(tls_head)-1;memcpy(native_slot->request.stream.headers,tls_head,sizeof(tls_head)-1);
    assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,a)==-13);
    native_slot->request.tls_verified=1;
    a[2]=sizeof(info);assert(!dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_POLL,a));
    memcpy(&info,user_memory+0x100,sizeof(info));assert(!info.tls_unverified);
    a[2]=4;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_READ,a)==4 && !memcmp(user_memory+0x100,"hell",4));
    assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_READ,a)==1);
    native_slot->request.status=SACX_NET_STATUS_DONE;native_slot->request.truncated=1;
    assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_READ,a)==-75);
    a[1]=a[2]=0;assert(!dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_CLOSE,a));
    memcpy(user_memory,"http://example.com/",20);a[0]=0x1000;a[1]=19;a[2]=1000;
    for(unsigned i=0;i<4;++i)assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_START,a)>0);
    assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_START,a)==-24);dihos_user_network_cleanup(0x101);
    // Redirects can exceed the former 767-byte limit. Preserve the entire
    // bounded URL, including the last byte, before returning to userspace.
    memset(user_memory,'a',SACX_USER_HTTP_URL_MAX);memcpy(user_memory,"https://www.google.com/search?q=",32);user_memory[SACX_USER_HTTP_URL_MAX]=0;
    a[0]=0x1000;a[1]=SACX_USER_HTTP_URL_MAX;a[2]=1000;
    auto long_id=dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_START_MANUAL,a);assert(long_id>0);
    auto *long_slot=user_http_find(0x101,long_id);assert(long_slot && strlen(long_slot->request.url)==SACX_USER_HTTP_URL_MAX && long_slot->request.url[SACX_USER_HTTP_URL_MAX-1]=='a');
    user_memory[SACX_USER_HTTP_URL_MAX-1]='Z';assert(long_slot->request.url[SACX_USER_HTTP_URL_MAX-1]=='a');dihos_user_network_cleanup(0x101);
    a[0]=0x1002;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_START_MANUAL,a)==-14);
    memcpy(user_memory,"http://example.com/",20);a[0]=0x1000;a[1]=19;
    a[0]=0;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_START,a)==-14);
    a[0]=0x1000;a[1]=SACX_USER_HTTP_URL_MAX+1;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_START,a)==-22);
    a[1]=19;user_memory[10]='\r';assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_START,a)==-22);
    memcpy(user_memory,"http://example.com/",20);fail_pages=true;
    assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_START,a)==-12);fail_pages=false;
    int64_t manual=dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_START_MANUAL,a);assert(manual>0);
    native_slot=user_http_find(0x101,manual);assert(native_slot && native_slot->request.no_follow);
    // Read beyond the former 64 KiB boundary without widening checked copies.
    assert(native_slot->request.capacity==SACX_HTTP_RESPONSE_MAX);
    memset(native_slot->request.buffer,0x5a,70000);native_slot->request.body_size=70000;
    uint64_t large_read[3]={(uint64_t)manual,0x1100,4096};
    unsigned total_read=0;
    while(total_read<70000){auto count=dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_READ,large_read);assert(count>0 && count<=4096);total_read+=(unsigned)count;assert(user_memory[0x100]==0x5a);}
    assert(total_read==70000);native_slot->request.body_size=0;native_slot->consumed=0;
    // Exercise the final checked copy beyond the previous response ceiling.
    const unsigned tail=12u*1024u*1024u;
    memset(native_slot->request.buffer+tail-4096,0x6b,4096);
    native_slot->request.body_size=tail;native_slot->consumed=tail-4096;
    assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_READ,large_read)==4096);
    assert(user_memory[0x100]==0x6b && native_slot->consumed==tail);
    native_slot->request.body_size=0;native_slot->consumed=0;
    header_args[0]=(uint64_t)manual;header_args[1]=0x1100;header_args[2]=7;
    assert(dihos_user_network_call(nullptr,0x102,SACX_USER_HTTP_HEADERS,header_args)==-9);
    assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,header_args)==-11);
    const char raw_headers[]="HTTP/1.1 302 Found\r\nLocation: /next\r\nContent-Length: 0\r\n\r\n";
    memcpy(native_slot->request.stream.headers,raw_headers,sizeof(raw_headers)-1);
    native_slot->request.stream.header_bytes=sizeof(raw_headers)-1;native_slot->request.stream.headers_ready=1;
    assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,header_args)==7);
    assert(!memcmp(user_memory+0x100,raw_headers,7) && !native_slot->consumed);
    header_args[2]=(7ull<<32)|64;
    assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,header_args)==sizeof(raw_headers)-1-7);
    assert(!memcmp(user_memory+0x100,raw_headers+7,sizeof(raw_headers)-1-7));
    header_args[2]=((uint64_t)(sizeof(raw_headers)-1)<<32)|1;
    assert(!dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,header_args));
    header_args[2]=((uint64_t)sizeof(raw_headers)<<32)|1;
    assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,header_args)==-22);
    header_args[2]=4097;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,header_args)==-22);
    header_args[2]=0;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,header_args)==-22);
    header_args[2]=1;header_args[1]=0;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,header_args)==-14);
    header_args[1]=0x2fff;header_args[2]=2;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,header_args)==-14);
    header_args[1]=0x1100;header_args[2]=1;
    native_slot->request.status=SACX_NET_STATUS_FAILED;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,header_args)==-5);
    native_slot->request.status=SACX_NET_STATUS_CANCELLED;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,header_args)==-125);
    native_slot->request.status=SACX_NET_STATUS_DONE;native_slot->request.stream.headers_ready=0;
    assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,header_args)==-71);
    native_slot->request.stream.headers_ready=1;native_slot->request.stream.header_bytes=sizeof(native_slot->request.stream.headers)+1;
    assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,header_args)==-5);
    dihos_user_network_cleanup(0x101);
    assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_HEADERS,header_args)==-9);
    // Descriptor, URL and fields must be copied before returning; malformed
    // inputs never allocate a request or enter the transport continuation.
    const char fields[]="Accept: text/html\r\nUser-Agent: DihOS native test\r\nOrigin: http://example.com\r\n";
    sacx_user_http_request request={0x1000,0x1400,19,sizeof(fields)-1,1000,0};
    memcpy(user_memory,"http://example.com/",20);memcpy(user_memory+0x400,fields,sizeof(fields));
    auto save_request=[&]{memcpy(user_memory+0x300,&request,sizeof(request));};save_request();
    uint64_t args[3]={0x1300,sizeof(request),0};prior=starts;
    auto extended=dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_REQUEST,args);
    assert(extended>0 && starts==prior);
    auto *extended_slot=user_http_find(0x101,extended);assert(extended_slot && extended_slot->request.no_follow);
    memset(user_memory+0x400,'X',sizeof(fields));user_memory[0]='X';memset(user_memory+0x300,0,sizeof(request));
    assert(!strcmp(extended_slot->request.url,"http://example.com/") &&
           extended_slot->request.request_header_bytes==sizeof(fields)-1 &&
           !strcmp(extended_slot->request.request_headers,fields));
    dihos_user_network_cleanup(0x101);memcpy(user_memory,"http://example.com/",20);
    const char *bad_fields[]={"Host: attacker\r\n","hOsT: attacker\r\n","Connection: close\r\n",
        "Content-Length: 3\r\n","Transfer-Encoding: chunked\r\n","Accept-Encoding: gzip\r\n",
        "Accept: one\r\naCcEpT: two\r\n","X-Test: ok\nInjected: yes\r\n"," X-Fold: value\r\n",
        "X-Test: ok\r\n\r\n","X-Test: missing CRLF","X-Test: a\001b\r\n","X Bad: value\r\n"};
    for(const char *bad:bad_fields){
        request.header_bytes=(uint32_t)strlen(bad);memcpy(user_memory+0x400,bad,request.header_bytes);save_request();
        assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_REQUEST,args)<0);
    }
    request.header_bytes=sizeof(fields)-1;memcpy(user_memory+0x400,fields,sizeof(fields));
    request.reserved=1;save_request();assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_REQUEST,args)==-22);
    request.reserved=0;request.headers=0x2fff;save_request();assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_REQUEST,args)==-14);
    request.headers=0x1400;request.header_bytes=SACX_USER_HTTP_REQUEST_HEADERS_MAX+1;save_request();assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_REQUEST,args)==-22);
    request.header_bytes=0;save_request();assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_REQUEST,args)==-22);
    request.headers=0;save_request();
    args[1]--;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_REQUEST,args)==-22);args[1]++;
    args[2]=1;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_REQUEST,args)==-22);args[2]=0;
    fail_pages=true;assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_REQUEST,args)==-12);fail_pages=false;
    extended=dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_REQUEST,args);assert(extended>0);
    dihos_user_network_cleanup(0x101);
    sacx_user_http_send post={0x1000,0,0x1800,19,0,4,1000,SACX_USER_HTTP_POST,0};
    memcpy(user_memory+0x800,"a=42",4);
    auto save_post=[&]{memcpy(user_memory+0x300,&post,sizeof(post));};save_post();
    uint64_t post_args[3]={0x1300,sizeof(post),0};
    auto posted=dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_SEND,post_args);assert(posted>0);
    auto* posted_slot=user_http_find(0x101,posted);assert(posted_slot && posted_slot->request.no_follow && !strcmp(posted_slot->request.method,"POST"));
    memset(user_memory+0x800,'x',4);memset(user_memory+0x300,0,sizeof(post));
    assert(posted_slot->request.request_body_size==4 && !memcmp(posted_slot->request.request_body,"a=42",4));
    dihos_user_network_cleanup(0x101);
    post.method=SACX_USER_HTTP_GET;save_post();assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_SEND,post_args)==-22);
    post.method=99;save_post();assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_SEND,post_args)==-22);
    post.method=SACX_USER_HTTP_POST;post.body_bytes=SACX_USER_HTTP_BODY_MAX+1;save_post();assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_SEND,post_args)==-22);
    post.body_bytes=4;post.body=0x2fff;save_post();assert(dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_SEND,post_args)==-14);
    post.body_bytes=0;post.body=0;save_post();posted=dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_SEND,post_args);assert(posted>0);
    dihos_user_network_cleanup(0x101);
    memset(large_user_memory,'a',SACX_USER_HTTP_REQUEST_HEADERS_MAX);memcpy(large_user_memory,"Cookie: x=",10);
    large_user_memory[SACX_USER_HTTP_REQUEST_HEADERS_MAX-2]='\r';large_user_memory[SACX_USER_HTTP_REQUEST_HEADERS_MAX-1]='\n';
    memset(large_user_memory+16384,0x7b,SACX_USER_HTTP_BODY_MAX);
    post.headers=0x10000;post.header_bytes=SACX_USER_HTTP_REQUEST_HEADERS_MAX;post.body=0x14000;post.body_bytes=SACX_USER_HTTP_BODY_MAX;save_post();
    posted=dihos_user_network_call(nullptr,0x101,SACX_USER_HTTP_SEND,post_args);assert(posted>0);
    posted_slot=user_http_find(0x101,posted);assert(posted_slot && posted_slot->request.request_header_bytes==16384 && posted_slot->request.request_body_size==16384);
    memset(large_user_memory,0,sizeof(large_user_memory));
    assert(posted_slot->request.request_headers[10000]=='a' && posted_slot->request.request_body[16383]==0x7b);
    dihos_user_network_cleanup(0x101);
    puts("PASS: 16 KiB request headers and 16 KiB POST bodies are owned; exact bounds and protected buffers enforced");
    puts("PASS: appended GET/POST descriptor owns body bytes, permits empty POST and rejects methods, oversized bodies and protected input");
    for(auto& slot:G_user_http)assert(!slot.owner && !slot.request.used);
    free(G_net_stack);
    puts("PASS: native owned URL, generation/process handles, checked reads, partial delivery, truncation, request quota and deferred active cleanup");
    puts("PASS: 8191-byte URLs retain final query bytes, owned copies survive mutation, oversized/protected buffers are rejected");
    puts("PASS: native 16 MiB bounded responses and protected 4 KiB reads beyond the former 8 MiB limit");
    puts("PASS: manual redirects, immutable bounded header slices, owner checks, EOF, invalid offsets and closed handles");
    puts("PASS: owned GET descriptor/header copies, malformed fields, duplicate names, framing overrides, protected buffers, OOM and capability admission");
    puts("PASS: queued owned requests, bounded core-0 slices, cancellation/release ordering and automatic DHCP continuation without remote workers");
}
