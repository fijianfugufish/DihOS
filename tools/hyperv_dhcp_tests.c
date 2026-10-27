#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/net/knet_usb.c"
static uint64_t now;
static int online=1,drop_replies;
static uint32_t delay_polls, empty_polls;
static uint8_t reply_frames[4][1536];
static uint32_t reply_lengths[4], reply_head, reply_count, sent;
/* Independent one's-complement wire validation, including the IPv4 pseudoheader. */
static uint32_t wire_sum(const uint8_t *p,uint32_t n,uint32_t sum) {
    for (uint32_t i=0;i<n;++i) sum+=(uint32_t)p[i] << ((i&1) ? 0:8);
    while (sum>>16) sum=(sum&65535)+(sum>>16);
    return sum;
}
void knet_link_diag_print(void) {}
static uint8_t fixture_mac[6]={2,1,2,3,4,5};
uint64_t dihos_time_monotonic_ms(void) { return now; }
int knet_link_is_hyperv(void) { return 1; }
uint32_t knet_link_online(void) { return online; }
int knet_link_get_mac(uint8_t mac[6]) { memcpy(mac,fixture_mac,6);return 0; }
#ifdef HYPERV_TLS_TEST
static int tls_fixture_send(const void *data,uint32_t n);
#endif
int knet_link_send_frame(const void *data,uint32_t n)
{
    const uint8_t *p=data; ++sent;
#ifdef HYPERV_TLS_TEST
    return tls_fixture_send(data,n);
#endif
    assert(n>=60);
    if (drop_replies || get_be16(p+12)!=0x0800 || p[23]!=17 || get_be16(p+36)!=67) return 0;
    assert(wire_sum(p+14,20,0)==65535);
    uint32_t udp_length=((uint32_t)p[38]<<8)|p[39];
    assert(udp_length+34==n);
    assert(wire_sum(p+34,udp_length,wire_sum(p+26,8,17+udp_length))==65535);
    for (unsigned i=0;i<6;++i) assert(p[i]==255 && p[6+i]==fixture_mac[i] && p[70+i]==fixture_mac[i]);
    assert(n>=282 && get_be16(p+34)==68 && p[42]==1 && get_be32(p+46)==g_dhcp_xid);
    uint8_t request_type=0;
    for (uint32_t i=282;i+1<n;) {
        uint8_t option=p[i++]; if (option==255) break; if (!option) continue;
        uint8_t len=p[i++];assert(i+len<=n);if (option==53 && len==1) request_type=p[i];i+=len;
    }
    assert(request_type==1 || request_type==3);
    uint8_t bootp[300]={0};memcpy(bootp,p+42,240);bootp[0]=2;
    put_be32(bootp+16,0x0a000002);
    uint8_t options[]={53,1,0,54,4,10,0,0,1,1,4,255,255,255,0,3,4,10,0,0,1,6,4,10,0,0,1,51,4,0,0,0,10,255};
    options[2]=request_type==1 ? 2:5;memcpy(bootp+240,options,sizeof(options));
    assert(reply_count<4);uint32_t slot=(reply_head+reply_count++)%4;
    reply_lengths[slot]=build_udp_ipv4(reply_frames[slot],fixture_mac,0x0a000001,0xffffffff,67,68,bootp,240+sizeof(options));
    return 0;
}
int knet_link_recv_frame(void *p,uint32_t cap,uint32_t *n) {
    if (!reply_count || delay_polls) { if (delay_polls) --delay_polls; ++empty_polls; ++now; *n=0;return 1; }
    *n=reply_lengths[reply_head];assert(cap>=*n);memcpy(p,reply_frames[reply_head],*n);
    reply_head=(reply_head+1)%4;--reply_count;return 0;
}
int kwork_submit(kwork_fn fn,void *ctx,uint32_t flags,uint32_t *out) { (void)fn;(void)ctx;(void)flags;*out=1;return 0; }
uint32_t kwork_status(uint32_t job) { (void)job;return KWORK_STATUS_DONE; }
uint64_t dihos_time_ticks(void) { return now*DIHOS_TIME_TICKS_PER_SECOND/1000; }
uint32_t knet_link_pending_frames(void) { return reply_count; }
void knet_link_discard_pending_frames(void) { reply_count=0; }
int knet_link_prepare_receive(void) { return online ? 0:-1; }
void knet_link_get_status(knet_link_status *s) { memset(s,0,sizeof(*s));s->online=(uint8_t)online;memcpy(s->mac,fixture_mac,6); }
void asm_dma_invalidate_range(const void *p,uint64_t n) { (void)p;(void)n; }
#undef terminal_print
#undef terminal_print_inline
#undef terminal_error
#undef terminal_warn
#undef terminal_success
#undef terminal_print_inline_hex32
#undef terminal_flush_log
void terminal_print(const char *s) { (void)s; }
void terminal_print_inline(const char *s) { (void)s; }
void terminal_error(const char *s) { (void)s; }
void terminal_warn(const char *s) { (void)s; }
void terminal_success(const char *s) { (void)s; }
void terminal_print_inline_hex32(uint32_t v) { (void)v; }
void terminal_flush_log(void) {}
#ifndef HYPERV_TLS_TEST
int main(void)
{
    knet_auto_step(0);
    assert(sent==2 && !g_auto_phase && g_knet.configured);
    assert(g_knet.ip==0x0a000002 && g_knet.mask==0xffffff00 && g_knet.router==0x0a000001 && g_knet.dns==0x0a000001);
    assert(g_auto_renew==5000);
    now=4999;knet_auto_step(0);assert(sent==2);
    now=5000;knet_auto_step(0);assert(sent==4 && g_knet.configured && g_auto_renew==10000);
    online=0;knet_auto_step(0);assert(!g_knet.configured && !g_auto_phase);
    online=1;now=6000;knet_auto_step(0);assert(sent==6 && g_knet.configured);
    online=0;knet_auto_step(0);online=1;drop_replies=1;
    for (uint32_t i=0;i<5;++i) { now=7000+i*1000;knet_auto_step(0); }
    assert(!g_auto_phase && !g_knet.configured && g_auto_deadline==41000);
    drop_replies=0;now=41000;knet_auto_step(0);assert(g_knet.configured);
    /* Regression: 500 empty nonblocking polls must not terminate a 1s wait. */
    now=50000; delay_polls=500; empty_polls=0;
    dhcp_parse_result delayed;
    assert(!dhcp_send(1,0,0) && !dhcp_wait(2,&delayed,1));
    assert(empty_polls==500 && now==50500 && delayed.yiaddr==0x0a000002);
    drop_replies=1;empty_polls=0;now=60000;
    assert(!dhcp_send(1,0,0));
    assert(dhcp_wait(2,&delayed,1)<0 && now>=61000 && now<61100 && empty_polls>=1000);
    puts("PASS: delayed DHCP after 500 empty polls, elapsed-time timeout, wire checksums; production automatic DHCP discover/offer/request/ack, gateway/DNS, T1 reacquisition, link recovery, timeout backoff and retry");
    return 0;
}

#endif
int dihos_time_utc(uint64_t *seconds) { *seconds=1792972800ull+now/1000;return 0; }
uint64_t dihos_time_seconds(void) { return now/1000; }


#ifndef HYPERV_TLS_TEST
/* TLS entry points are outside this DHCP test; reaching one is a failure. */
void br_ssl_client_init_full(br_ssl_client_context *cc,
	br_x509_minimal_context *xc,
	const br_x509_trust_anchor *trust_anchors, size_t trust_anchors_num) { assert(0); }
void br_ssl_engine_set_buffer(br_ssl_engine_context *cc,
	void *iobuf, size_t iobuf_len, int bidi) { assert(0); }
void br_ssl_engine_inject_entropy(br_ssl_engine_context *cc,
	const void *data, size_t len) { assert(0); }
int br_ssl_client_reset(br_ssl_client_context *cc,
	const char *server_name, int resume_session) { assert(0); return 0; }
unsigned br_ssl_engine_current_state(const br_ssl_engine_context *cc) { assert(0); return 0; }
unsigned char *br_ssl_engine_sendrec_buf(
	const br_ssl_engine_context *cc, size_t *len) { assert(0); return 0; }
void br_ssl_engine_sendrec_ack(br_ssl_engine_context *cc, size_t len) { assert(0); }
unsigned char *br_ssl_engine_sendapp_buf(
	const br_ssl_engine_context *cc, size_t *len) { assert(0); return 0; }
void br_ssl_engine_sendapp_ack(br_ssl_engine_context *cc, size_t len) { assert(0); }
unsigned char *br_ssl_engine_recvrec_buf(
	const br_ssl_engine_context *cc, size_t *len) { assert(0); return 0; }
void br_ssl_engine_recvrec_ack(br_ssl_engine_context *cc, size_t len) { assert(0); }
unsigned char *br_ssl_engine_recvapp_buf(
	const br_ssl_engine_context *cc, size_t *len) { assert(0); return 0; }
void br_ssl_engine_recvapp_ack(br_ssl_engine_context *cc, size_t len) { assert(0); }
void br_ssl_engine_flush(br_ssl_engine_context *cc, int force) { assert(0); }

#endif
