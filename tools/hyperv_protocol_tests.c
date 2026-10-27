#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hyperv/hv_hid.h"
#include "hyperv/hv_wire.h"
/* Exercise the production parsers and state machines against fake channels. */
#include "../kernel/hyperv/hyperv_input.c"
#include "../kernel/hyperv/hyperv_net.c"

static uint64_t clock_ms;
static uint8_t incoming[64][4096];
static uint32_t incoming_bytes[64], in_head, in_count, ack_count, last_ack;
static uint32_t mouse_index, mouse_total;
static int lose_completion, init_mode, query_reply_mode;
static uint32_t init_requests, ndis_requests;
static const uint8_t absolute_desc[]={
    0x05,1,0x09,2,0xa1,1,0x85,3,0x09,1,0xa1,0,
    0x05,9,0x19,1,0x29,3,0x15,0,0x25,1,0x75,1,0x95,3,0x81,2,
    0x75,5,0x95,1,0x81,1,0x05,1,0x09,0x30,0x09,0x31,
    0x15,0,0x26,0xff,0xff,0x75,16,0x95,2,0x81,2,
    0x09,0x38,0x15,0x81,0x25,0x7f,0x75,8,0x95,1,0x81,6,0xc0,0xc0
};

uint64_t dihos_time_monotonic_ms(void) { return ++clock_ms; }
void asm_relax(void) {}
void knet_link_checkpoint(void) {}
void asm_dma_clean_range(const void *p, uint64_t n) { (void)p;(void)n; }
void asm_dma_invalidate_range(const void *p, uint64_t n) { (void)p;(void)n; }
void *pmem_alloc_pages(uint64_t n) { void *p=malloc((size_t)n*4096); if (p) memset(p,0,(size_t)n*4096); return p; }
void terminal_warn(const char *s) { (void)s; }
void terminal_error(const char *s) { (void)s; }
void terminal_success(const char *s) { (void)s; }
void terminal_print(const char *s) { (void)s; }
void terminal_print_inline(const char *s) { (void)s; }
void terminal_print_inline_hex32(uint32_t n) { (void)n; }
void hyperv_trace(const char *s,int32_t a,int32_t b,int32_t c,int32_t d) { (void)s;(void)a;(void)b;(void)c;(void)d; }
int vmbus_open_keyboard_channel(void) { return -1; }
int vmbus_open_mouse_channel(void) { return -1; }
int vmbus_keyboard_send_inband(const void *p,uint32_t n) { (void)p;(void)n;return -1; }
int vmbus_keyboard_receive(uint8_t *p,uint32_t c,uint32_t *n) { (void)p;(void)c;*n=0;return 1; }
int vmbus_mouse_send_inband(const void *p,uint32_t n) { (void)p;(void)n;return 0; }
int vmbus_mouse_receive(uint8_t *p,uint32_t c,uint32_t *n)
{
    assert(c>=23);
    if (mouse_index==mouse_total) return 1;
    memset(p,0,23); hv_put32(p,1); hv_put32(p+4,15); hv_put32(p+8,4); hv_put32(p+12,7);
    p[16]=3; p[17]=(uint8_t)((mouse_index%2)==0);
    hv_put16(p+18,(uint16_t)(1000+1000*(mouse_index/2))); hv_put16(p+20,2000);
    ++mouse_index; *n=23; return 0;
}
void vmbus_device_diag_print(uint32_t d) { (void)d; }
int vmbus_device_open(uint32_t d) { assert(d==0); return 0; }
int vmbus_device_share(uint32_t d,void *p,uint32_t n,uint32_t *h) { assert(d==0 && p && n==512); *h=42;return 0; }

static uint8_t *queue_packet(uint16_t type,uint32_t off,uint32_t payload_bytes,uint64_t tid)
{
    assert(in_count<64);
    uint32_t slot=(in_head+in_count++)%64;
    uint8_t *p=incoming[slot]; memset(p,0,4096);
    incoming_bytes[slot]=(off+payload_bytes+7)&~7u;
    hv_put16(p,type); hv_put16(p+2,(uint16_t)(off/8)); hv_put16(p+4,(uint16_t)(incoming_bytes[slot]/8)); hv_put64(p+8,tid);
    return p;
}
static void queue_rndis(const uint8_t *data,uint32_t n)
{
    /* Each pending transfer owns a distinct receive-buffer range. */
    static uint32_t rx_slot;
    uint32_t start=(rx_slot++%256)*4096;
    memcpy(rx_buffer+start,data,n);
    uint8_t *p=queue_packet(7,32,16,1000+rx_slot);
    hv_put16(p+16,0xcafe); hv_put32(p+20,1); hv_put32(p+24,n); hv_put32(p+28,start); hv_put32(p+32,107);
}
int vmbus_device_send(uint32_t d,uint16_t type,uint64_t tid,const void *payload,uint32_t bytes,const void *gpa,uint32_t gpa_bytes)
{
    const uint8_t *p=payload; assert(d==0 && bytes==32);
    /* Golden envelope size/reserved tail, independent of driver field copies. */
    for (uint32_t i=16;i<32;++i) assert(p[i]==0);
    uint32_t kind=hv_get32(p);
    if (type==11) { assert(kind==108); ++ack_count; last_ack=hv_get32(p+4); return 0; }
    if (kind==1) {
        ++init_requests;
        assert((type&VMBUS_SEND_REQUEST_COMPLETION) && hv_get32(p+4)==2 && hv_get32(p+8)==2);
        if (init_mode==3) return 0; /* silent host timeout */
        uint8_t *r=queue_packet(11,16,init_mode==2 ? 8:16,tid)+16;
        hv_put32(r,2);hv_put32(r+4,2);hv_put32(r+12,init_mode==1 ? 7:1);
    }
    if (kind==100) { ++ndis_requests;assert(hv_get32(p+4)==6 && hv_get32(p+8)==1); }
    if (kind==101) { uint8_t *r=queue_packet(11,16,28,tid)+16; hv_put32(r,102);hv_put32(r+4,1);hv_put32(r+8,1);hv_put32(r+16,4096);hv_put32(r+20,512);hv_put32(r+24,512*4096); }
    if (kind==107) {
        const uint8_t *req=gpa; assert(req && gpa_bytes>=8);
        uint32_t rtype=hv_get32(req);
        if (rtype!=1) {
            uint8_t answer[64]={0};
            hv_put32(answer,rtype|0x80000000u);hv_put32(answer+8,hv_get32(req+8));
            uint32_t size=rtype==2 ? 52 : 16;
            if (rtype==4) {
                assert(gpa_bytes==28 && hv_get32(req+16)==0 && hv_get32(req+20)==20 && hv_get32(req+24)==0);
                uint32_t oid=hv_get32(req+12); size=oid==0x01010101 ? 30 : 28;
                hv_put32(answer+16,size-24); hv_put32(answer+20,16);
                if (oid==0x01010101) { uint8_t mac[6]={2,1,2,3,4,5};memcpy(answer+24,mac,6); }
                if (query_reply_mode==1) hv_put32(answer+12,0xc000000d); /* rejected query */
                if (query_reply_mode==2) hv_put32(answer+8,hv_get32(req+8)+99); /* unrelated request */
                if (query_reply_mode==3) size=16; /* missing QUERY_CMPLT fields */
                if (query_reply_mode==4) hv_put32(answer+20,0); /* points into completion header */
                if (query_reply_mode==5) {
                    memmove(answer+32,answer+24,size-24);size+=8;hv_put32(answer+20,24);
                }
                if (query_reply_mode==6) hv_put32(answer+16,4); /* short MAC */
            }
            hv_put32(answer+4,size);queue_rndis(answer,size);
        }
        if (!lose_completion) { uint8_t *r=queue_packet(11,16,8,tid)+16; hv_put32(r,108);hv_put32(r+4,1); }
    }
    return 0;
}
int vmbus_device_receive(uint32_t d,void *p,uint32_t cap,uint32_t *n)
{
    assert(d==0);
    if (!in_count) { *n=0;return 1; }
    *n=incoming_bytes[in_head];assert(cap>=*n);memcpy(p,incoming[in_head],*n);
    in_head=(in_head+1)%64;--in_count;return 0;
}

static void test_hid(void)
{
    hv_hid_layout layout; hv_pointer_report event;
    assert(!hv_hid_parse(&layout,absolute_desc,sizeof(absolute_desc)));
    uint8_t data[7]={3,1,0xff,0x7f,0xff,0xff,0xff};
    assert(!hv_hid_decode(&layout,data,7,&event));
    assert(event.buttons==1 && event.x==16383 && event.y==32767 && event.wheel==-1 && event.absolute);
    data[2]=0; data[3]=0x80;
    assert(!hv_hid_decode(&layout,data,7,&event) && event.x==16383); /* no discontinuity at 0x8000 */
    data[0]=4; assert(hv_hid_decode(&layout,data,7,&event));
    data[0]=3; assert(hv_hid_decode(&layout,data,6,&event));
    uint8_t truncated[]={0x27,1,2}; assert(hv_hid_parse(&layout,truncated,sizeof(truncated)));
    uint8_t pop[]={0xb4};assert(hv_hid_parse(&layout,pop,sizeof(pop)));
    uint8_t relative[]={0x05,1,0x09,0x30,0x09,0x31,0x15,0x81,0x25,0x7f,0x75,8,0x95,2,0x81,6};
    assert(!hv_hid_parse(&layout,relative,sizeof(relative)));
    uint8_t movement[]={0x81,0x7f};assert(!hv_hid_decode(&layout,movement,2,&event));
    assert(event.x==-127 && event.y==127 && !event.absolute);
}
static void test_click_order(void)
{
    assert(!hv_hid_parse(&G_mouse_layout,absolute_desc,sizeof(absolute_desc)));
    G_hv_mouse_online=1;G_hv_keyboard_online=0;
    G_pointer_count=G_pointer_head=0;mouse_index=0;mouse_total=128;
    for (uint32_t i=0;i<128;++i) {
        hyperv_input_poll();
        assert(G_hv_mouse_buttons==((i%2)==0));
        uint32_t source=(uint16_t)(1000+1000*(i/2));
        assert(G_hv_mouse_x==(int32_t)((uint64_t)source*32767/65535));
    }
    assert(!G_pointer_count && mouse_index==128);
    hyperv_input_poll();assert(G_hv_mouse_buttons==0);
}
static void test_network(void)
{
    for (init_mode=1;init_mode<=3;++init_mode) {
        uint32_t before=ndis_requests;uint64_t start=clock_ms;
        assert(hyperv_net_init()<0 && !hyperv_net_online());
        assert(ndis_requests==before && clock_ms-start<2100);
        assert(!rx_buffer && !tx_buffer);failed=0;
    }
    init_mode=0;
    assert(!hyperv_net_init());assert(hyperv_net_online());
    assert(init_requests==4 && ndis_requests==1);
    uint8_t mac[6];assert(!hyperv_net_mac(mac) && mac[0]==2 && mac[5]==5);
    for (query_reply_mode=1;query_reply_mode<=6;++query_reply_mode) {
        uint64_t start=clock_ms;int result=control(4,0x01010101,0);
        if (query_reply_mode<=2) assert(result<0 && clock_ms-start<2100);
        else if (query_reply_mode==5) {
            assert(!result && query_data(6) && query_data(6)[0]==2 && query_data(6)[5]==5);
        } else assert(!result && !query_data(6));
    }
    query_reply_mode=0;
    assert(!control(4,0x01010101,0) && query_data(6) && query_data(6)[5]==5);
    hv_put32(response+20,0xfffffff0u);assert(!query_data(6));
    assert(!control(4,0x01010101,0));--response_size;assert(!query_data(6));
    uint8_t message[104]={0};hv_put32(message,1);hv_put32(message+4,sizeof(message));hv_put32(message+8,36);hv_put32(message+12,60);
    for (unsigned i=0;i<60;++i) message[44+i]=(uint8_t)i;
    queue_rndis(message,sizeof(message));
    uint8_t frame[1514];uint32_t n;assert(!hyperv_net_receive(frame,sizeof(frame),&n) && n==60 && frame[59]==59);
    assert(!hyperv_net_send(frame,60));
    assert(data_tx==1 && data_tx_ok==1 && !data_tx_failed && ethernet_rx==1);
    hv_put32(message+8,0xfffffff8u);queue_rndis(message,sizeof(message));hyperv_net_poll();assert(!count);
    uint8_t *p=queue_packet(7,32,16,999);hv_put16(p+16,0xcafe);hv_put32(p+20,1);hv_put32(p+24,4096);hv_put32(p+28,NET_RX_PAGES*4096-1);hv_put32(p+32,107);
    hyperv_net_poll();assert(last_ack==6 && malformed_rx==2);
    uint8_t state[12]={0};hv_put32(state,7);hv_put32(state+4,12);hv_put32(state+8,0x4001000c);queue_rndis(state,12);hyperv_net_poll();assert(!hyperv_net_online());
    hv_put32(state+8,0x4001000b);queue_rndis(state,12);hyperv_net_poll();assert(hyperv_net_online());
    lose_completion=1;assert(hyperv_net_send(frame,60)<0 && failed && !hyperv_net_online());
    assert(ack_count>=7 && data_tx==2 && data_tx_failed==1 && last_tx_status==0xffffffffu);
}
static void test_bounds(void)
{
    uint8_t p[64]={0};uint32_t n=0,off=0;
    hv_put16(p+2,2);hv_put16(p+4,4);assert(!hv_packet_bounds(p,40,32,&n) && n==32);
    assert(hv_packet_bounds(p,39,32,&n));hv_put16(p+2,1);assert(hv_packet_bounds(p,40,32,&n));
    assert(!hv_range(64,0xffffffffu,16));
    hv_put32(p,1);hv_put32(p+4,64);hv_put32(p+8,36);hv_put32(p+12,20);
    assert(!hv_rndis_frame(p,64,&off,&n) && off==44 && n==20);
    hv_put32(p+12,21);assert(hv_rndis_frame(p,64,&off,&n));
}
int main(void)
{
    test_hid();test_click_order();test_bounds();test_network();
    puts("PASS: HID ranges/report IDs/truncation; 128 ordered pointer transitions; packet bounds; NetVSC negotiation/RNDIS query wire offsets, rejected/stale/truncated/padded replies, RX completion/link loss/TX timeout");
    return 0;
}
