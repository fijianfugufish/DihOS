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
static uint64_t now;
static unsigned starts,polls,quiet;
static unsigned background_calls,background_steps;
static void (*checkpoint)(void);
static uint64_t dihos_time_monotonic_ms() {return now;}
static uint32_t smp_current_logical_id() {return 0;}
static void *pmem_alloc_pages(uint64_t n) {return malloc((size_t)n*4096);}
static void pmem_free_pages(void *p,uint64_t) {free(p);}
static void asm_dma_clean_range(const void*,uint64_t) {}
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
    char too_long[769];memset(too_long,'x',768);too_long[768]=0;desc.url=too_long;
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
    free(G_net_stack);
    puts("PASS: queued owned requests, bounded core-0 slices, cancellation/release ordering and automatic DHCP continuation without remote workers");
}
