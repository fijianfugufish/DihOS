/* Exercise the real HTTPS transport loop with a deterministic TLS engine.
 * This tests scheduling and TCP delivery, not cryptography or certificate trust. */
#define HYPERV_TLS_TEST 1
#include "hyperv_dhcp_tests.c"
static unsigned tls_phase, no_server_reply;
static uint8_t tls_wire[4096];
static const uint8_t application[]="HTTP/1.0 200 OK\r\n\r\nhello";

static uint8_t *new_reply(uint32_t size)
{
    assert(reply_count<4);
    uint32_t slot=(reply_head+reply_count++)%4;
    reply_lengths[slot]=size;
    memset(reply_frames[slot],0,size);
    return reply_frames[slot];
}
static int tls_fixture_send(const void *data,uint32_t n)
{
    const uint8_t *p=data;
    if (get_be16(p+12)==0x0806) {
        uint8_t *r=new_reply(n);memcpy(r,p,n);
        put_be16(r+20,2);memcpy(r+22,fixture_mac,6);
        memcpy(r+28,p+38,4);memcpy(r+38,p+28,4);
        return 0;
    }
    assert(p[23]==6 && n>=54);
    unsigned flags=p[47], header=(p[46]>>4)*4;
    unsigned bytes=get_be16(p+16)-20-header;
    if (!(flags&2) && !bytes) return 0;
    if (no_server_reply && bytes) return 0;
    uint8_t *r=new_reply(54+(bytes ? 1:0));
    put_be16(r+12,0x0800);r[14]=0x45;r[23]=6;
    put_be16(r+16,40+(bytes ? 1:0));
    memcpy(r+26,p+30,4);memcpy(r+30,p+26,4);
    memcpy(r+34,p+36,2);memcpy(r+36,p+34,2);
    put_be32(r+38,bytes ? 1001:1000);
    put_be32(r+42,get_be32(p+38)+(bytes ? bytes:1));
    r[46]=0x50;r[47]=bytes ? 0x18:0x12;
    if (bytes) { r[54]=42;delay_polls=9000; }
    return 0;
}
void br_ssl_client_init_full(br_ssl_client_context *c,br_x509_minimal_context *x,
    const br_x509_trust_anchor *a,size_t n) { (void)c;(void)x;(void)a;(void)n; }
void br_ssl_engine_set_buffer(br_ssl_engine_context *c,void *p,size_t n,int b)
{ (void)c;(void)p;(void)n;(void)b; }
void br_ssl_engine_inject_entropy(br_ssl_engine_context *c,const void *p,size_t n)
{ (void)c;(void)p;(void)n; }
int br_ssl_client_reset(br_ssl_client_context *c,const char *s,int r)
{ (void)c;(void)s;(void)r;tls_phase=0;return 1; }
unsigned br_ssl_engine_current_state(const br_ssl_engine_context *c)
{ (void)c;return (unsigned[]){BR_SSL_SENDREC,BR_SSL_RECVREC,BR_SSL_SENDAPP,BR_SSL_RECVAPP,BR_SSL_CLOSED}[tls_phase]; }
unsigned char *br_ssl_engine_sendrec_buf(const br_ssl_engine_context *c,size_t *n)
{ (void)c;*n=1;return tls_wire; }
void br_ssl_engine_sendrec_ack(br_ssl_engine_context *c,size_t n)
{ (void)c;assert(n==1);tls_phase=1; }
unsigned char *br_ssl_engine_sendapp_buf(const br_ssl_engine_context *c,size_t *n)
{ (void)c;*n=sizeof(tls_wire);return tls_wire; }
void br_ssl_engine_sendapp_ack(br_ssl_engine_context *c,size_t n)
{ (void)c;assert(n);tls_phase=3; }
void br_ssl_engine_flush(br_ssl_engine_context *c,int f) { (void)c;(void)f; }
unsigned char *br_ssl_engine_recvrec_buf(const br_ssl_engine_context *c,size_t *n)
{ (void)c;*n=sizeof(tls_wire);return tls_wire; }
void br_ssl_engine_recvrec_ack(br_ssl_engine_context *c,size_t n)
{ (void)c;assert(n==1 && tls_wire[0]==42);tls_phase=2; }
unsigned char *br_ssl_engine_recvapp_buf(const br_ssl_engine_context *c,size_t *n)
{ (void)c;*n=sizeof(application)-1;return (unsigned char *)application; }
void br_ssl_engine_recvapp_ack(br_ssl_engine_context *c,size_t n)
{ (void)c;assert(n==sizeof(application)-1);tls_phase=4; }
int main(void)
{
    memcpy(g_knet.mac,fixture_mac,6);g_knet.ip=0x0a000002;
    g_knet.mask=0xffffff00;g_knet.configured=1;
    assert(!knet_usb_request_url("https://10.0.0.1/",8192,"GET",0,0,0));
    assert(tls_phase==4 && empty_polls==9000 && now==9000);
    assert(g_tls_x509.days==719528u+1792972800u/86400 && g_tls_x509.seconds==0);
    no_server_reply=1;empty_polls=0;uint64_t start=now;
    assert(knet_usb_request_url("https://10.0.0.1/",8192,"GET",0,0,0)<0);
    assert(now-start>=30000 && now-start<30100 && !br_ssl_engine_last_error(&g_tls_client.eng));
    puts("PASS: production HTTPS loop waits through 9000 empty polls for delayed TLS data; silent peer reaches 30s deadline; mock engine, no cryptographic validation");
    return 0;
}
