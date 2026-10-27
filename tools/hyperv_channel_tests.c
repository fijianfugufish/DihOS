#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "hyperv/hv_wire.h"
#include "hyperv/hv_ring.h"
#include "hyperv/vmbus.h"
#define VMBUS_RING_PAGES 8u
#define VMBUS_RING_DOWNSTREAM_OFFSET 4u
#define VMBUS_RING_DATA_SIZE (3u*4096)
#define VMBUS_MESSAGE_SINT 2u
#define VMBUS_CHANNEL_OFFER 1u
#define VMBUS_CHANNEL_OPEN 5u
#define VMBUS_CHANNEL_OPEN_RESULT 6u
#define VMBUS_CHANNEL_GPADL_HEADER 8u
#define VMBUS_CHANNEL_GPADL_CREATED 10u
#define HV_REGISTER_EOM 0x000a0014u
#define HV_CALL_POST_MESSAGE 0x5cu
#define HV_MESSAGE_TYPE_CHANNEL 1u
#define HV_CALL_SIGNAL_EVENT 0x5du
#define HV_HYPERCALL_FAST_BIT (1ull<<16)
#define vmbus_get_u16 hv_get16
#define vmbus_put_u16 hv_put16
#define vmbus_put_u32 hv_put32
#define vmbus_put_u64 hv_put64
static uint8_t messages[4096], event_page[4096];
static uint8_t *G_message_page=messages, *G_event_page=event_page;
static uint32_t G_vmbus_message_connection=4;
static uint8_t responses[8][32];
static uint32_t response_head,response_count,body_pages,body_expected,body_messages;
static uint32_t signal_count, allocations;
static uint32_t post_attempts, force_status, force_count, body_pressure, body_retry;
static uint32_t creation_status, drop_creation, current_relid, current_gpadl;
static uint64_t first_pfn;
static const uint8_t *input_pages[512];
static uint8_t input_copies[512][256];
static uint32_t input_count;
static void queue_response(uint32_t type,uint32_t child,uint32_t id);
static void queue_creation(void)
{
    if (drop_creation) return;
    queue_response(10,current_relid+99,current_gpadl);
    queue_response(10,current_relid,current_gpadl);
    hv_put32(responses[(response_head+response_count-1)%8]+16,creation_status);
}
static uint64_t fake_time;
static void (*on_signal)(void), (*on_poll)(void);
static uint32_t vmbus_get_u32(const volatile uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static int vmbus_guid_equal(const volatile uint8_t *p,const uint8_t *q)
{ for (uint32_t i=0;i<16;++i) if (p[i]!=q[i]) return 0; return 1; }
static uint32_t vmbus_ring_copy_to(uint8_t *ring,uint32_t off,const uint8_t *p,uint32_t n)
{ for (uint32_t i=0;i<n;++i) { ring[4096+off]=p[i];off=(off+1)%VMBUS_RING_DATA_SIZE; } return off; }
static void vmbus_ring_copy_from(const uint8_t *ring,uint32_t off,uint8_t *p,uint32_t n)
{ for (uint32_t i=0;i<n;++i) { p[i]=ring[4096+off];off=(off+1)%VMBUS_RING_DATA_SIZE; } }
static void vmbus_release_message(volatile uint8_t *p) { p[0]=0; }
static void queue_response(uint32_t type,uint32_t child,uint32_t id)
{
    assert(response_count<8);uint8_t *r=responses[(response_head+response_count++)%8];memset(r,0,32);
    hv_put32(r,type);hv_put32(r+8,child);hv_put32(r+12,id);
}
static uint64_t host_post_channel_message(uint32_t connection,const uint8_t *p,uint32_t n)
{
    assert(connection==4 && n<=240);
    uint32_t kind=hv_get32(p);
    if (kind==8) {
        body_expected=(hv_get16(p+16)-8)/8;body_pages=(n-28)/8;
        assert(hv_get32(p+20)==body_expected*4096 && hv_get32(p+24)==0);
        assert(hv_get16(p+18)==1 && body_pages<=26);
        current_relid=hv_get32(p+8);current_gpadl=hv_get32(p+12);first_pfn=hv_get64(p+28);
        for (uint32_t i=0;i<body_pages;++i) assert(hv_get64(p+28+i*8)==first_pfn+i);
        if (body_pages==body_expected) queue_creation();
    } else if (kind==9) {
        assert(hv_get32(p+8)==0 && hv_get32(p+12)==current_gpadl);
        for (uint32_t i=0;i<(n-16)/8;++i) assert(hv_get64(p+16+i*8)==first_pfn+body_pages+i);
        body_pages+=(n-16)/8;++body_messages;assert(body_pages<=body_expected);
        if (body_pages==body_expected) queue_creation();
    }
    else if (kind==5) { queue_response(6,hv_get32(p+8),hv_get32(p+12)+99);queue_response(6,hv_get32(p+8),hv_get32(p+12)); }
    return 0;
}
static uint64_t hyperv_hypercall(uint64_t call,uint64_t a,uint64_t b)
{
    (void)b;
    if (call==HV_CALL_POST_MESSAGE) {
        const uint8_t *input=(const uint8_t *)(uintptr_t)a; ++post_attempts;
        assert(hv_get32(input+8)==1);
        assert(input_count<512);
        for (uint32_t i=0;i<input_count;++i) assert(input_pages[i]!=input);
        input_pages[input_count]=input;memcpy(input_copies[input_count++],input,256);
        if (force_count) { --force_count;return force_status; }
        if (body_pressure && hv_get32(input+16)==9 && body_retry++<2) return body_retry==1 ? 0xb:0x13;
        body_retry=0;
        return host_post_channel_message(hv_get32(input),input+16,hv_get32(input+12));
    }
    ++signal_count;if (on_signal) on_signal();return 0;
}
static int hyperv_set_vpreg(uint32_t reg,uint64_t value)
{
    (void)value;assert(reg==HV_REGISTER_EOM);
    uint8_t *m=messages+512;
    if (!m[0] && response_count) {
        m[0]=1;memcpy(m+16,responses[response_head],32);response_head=(response_head+1)%8;--response_count;
    }
    return 0;
}
static uint64_t dihos_time_monotonic_ms(void) { if (on_poll) on_poll();return ++fake_time; }
static void asm_relax(void) {}
static void asm_dma_clean_range(const void *p,uint64_t n) { (void)p;(void)n; }
static void asm_dma_invalidate_range(const void *p,uint64_t n) { (void)p;(void)n; }
static void terminal_print(const char *s) { (void)s; }
static void terminal_error(const char *s) { (void)s; }
static void *pmem_alloc_pages(uint64_t n) { ++allocations;void *p=_aligned_malloc((size_t)n*4096,4096);assert(p);memset(p,0,(size_t)n*4096);return p; }
static void vmbus_zero_page(uint8_t *p) { memset(p,0,4096); }
static void terminal_print_inline(const char *s) { (void)s; }
static void terminal_print_inline_hex32(uint32_t n) { (void)n; }
static uint64_t pmem_virt_to_phys(const void *p) { return (uintptr_t)p; }
#include "../kernel/hyperv/vmbus_ring_io.inc"
#include "../kernel/hyperv/vmbus_post_message.inc"
#include "../kernel/hyperv/vmbus_devices.inc"
int main(void)
{
    uint8_t offer[200]={0};memcpy(offer+8,G_device_guids[0],16);hv_put32(offer+184,17);hv_put32(offer+192,8);vmbus_device_offer(offer);
    memcpy(offer+8,G_device_guids[1],16);hv_put32(offer+184,19);hv_put32(offer+192,9);vmbus_device_offer(offer);
    assert(!vmbus_device_open(0) && G_devices[0].opened && !G_devices[1].opened);
    assert(!vmbus_device_open(0) && allocations-input_count==1); /* no duplicate GPADL allocation */
    void *shared=pmem_alloc_pages(512);uint32_t handle;
    body_pressure=1;
    assert(!vmbus_device_share(0,shared,512,&handle) && body_pages==body_expected && body_pages==512 && body_messages==18);
    assert(G_devices[0].share_retries==36 && G_devices[0].share_sent==512 && !G_devices[0].share_status);
    body_pressure=0;
    assert(!vmbus_device_share(0,shared,26,&handle) && body_pages==26);
    assert(!vmbus_device_share(0,shared,27,&handle) && body_pages==27);
    uint32_t attempts=post_attempts, live=allocations-input_count;handle=0;
    assert(vmbus_device_share(0,shared,513,&handle)<0 && post_attempts==attempts && !handle);
    assert(vmbus_device_share(0,(uint8_t *)shared+1,16,&handle)<0 && post_attempts==attempts);
    force_status=5;force_count=1;
    assert(vmbus_device_share(0,shared,16,&handle)<0 && post_attempts==attempts+1 && allocations-input_count==live && !handle);
    assert(G_devices[0].share_status==5 && !G_devices[0].share_sent);
    force_status=0x12;force_count=2;assert(!vmbus_device_share(0,shared,16,&handle) && G_devices[0].share_retries==2);
    force_status=0x13;force_count=1000;attempts=post_attempts;uint64_t start_time=fake_time;handle=0;
    assert(vmbus_device_share(0,shared,16,&handle)<0 && post_attempts-attempts<=64 && fake_time-start_time<1100 && !handle && allocations-input_count==live);
    force_count=0;creation_status=0xc000009a;
    assert(vmbus_device_share(0,shared,16,&handle)<0 && G_devices[0].share_status==creation_status && !handle);
    creation_status=0;drop_creation=1;start_time=fake_time;
    assert(vmbus_device_share(0,shared,16,&handle)<0 && fake_time-start_time<2100 && G_devices[0].share_status==0xffffffffu && !handle);
    drop_creation=0;assert(!vmbus_device_share(0,shared,16,&handle));
    assert(!vmbus_device_open(1));
    /* Storage/HID transport stays one-shot even under transient pressure. */
    uint8_t legacy[8]={0};uint32_t before=post_attempts;
    force_status=0x13;force_count=1;
    assert((uint16_t)vmbus_post_channel_message(4,legacy,sizeof(legacy))==0x13 && post_attempts==before+1);
    for (uint32_t i=0;i<input_count;++i) assert(!memcmp(input_pages[i],input_copies[i],256));
    uint8_t data[32]={1},packet[64];uint32_t bytes;
    assert(!vmbus_device_send(0,6,123,data,8,0,0));
    vmbus_ring_copy_from(G_devices[0].ring,0,packet,24);
    assert(hv_get16(packet)==6 && hv_get16(packet+6)==0 && hv_get64(packet+8)==123);
    hv_put32(G_devices[0].ring,0);hv_put32(G_devices[0].ring+4,0);
    assert(!vmbus_device_send(0,6|VMBUS_SEND_REQUEST_COMPLETION,124,data,8,0,0));
    vmbus_ring_copy_from(G_devices[0].ring,0,packet,24);assert(hv_get16(packet)==6 && hv_get16(packet+6)==1);
    hv_put32(G_devices[0].ring,0);hv_put32(G_devices[0].ring+4,24);uint32_t signals=signal_count;
    assert(vmbus_device_send(0,6,125,data,32,0,0)==1 && signal_count==signals); /* no overwrite */
    uint8_t *ring=G_devices[1].ring+4*4096;memset(packet,0,32);
    hv_put16(packet,6);hv_put16(packet+2,2);hv_put16(packet+4,3);hv_put64(packet+8,321);
    uint32_t start=VMBUS_RING_DATA_SIZE-8;
    vmbus_ring_copy_to(ring,start,packet,32);hv_put32(ring+4,start);hv_put32(ring,24);
    assert(!vmbus_device_receive(1,packet,64,&bytes) && bytes==24 && hv_get64(packet+8)==321 && hv_get32(ring+4)==24);
    hv_put16(packet+2,1);vmbus_ring_copy_to(ring,24,packet,32);hv_put32(ring,56);
    assert(vmbus_device_receive(1,packet,64,&bytes)<0 && !G_devices[1].opened && G_devices[1].failed);
    uint8_t flow[4096]={0};hv_ring_store(flow+64,1);hv_ring_store(flow,VMBUS_RING_DATA_SIZE-32);hv_ring_store(flow+12,100);
    assert(hv_ring_notify_read(flow,0,88,VMBUS_RING_DATA_SIZE));
    assert(!hv_ring_notify_read(flow,88,176,VMBUS_RING_DATA_SIZE)); /* no duplicate wakeup */
    hv_ring_store(flow+8,1);assert(!hv_ring_notify_write(flow,176)); /* host draining */
    puts("PASS: VMBus offer routing, matched channel/request IDs, multi-message GPADL with transient pressure/backoff, permanent rejection/timeout, PFN continuity, pinned immutable input pages, legacy transport isolation, completion flags, send backpressure, wrapped receive, malformed-channel shutdown");
    return 0;
}
