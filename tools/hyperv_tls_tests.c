/* Exercise the real HTTPS transport loop with a deterministic TLS engine.
 * This tests scheduling and TCP delivery, not cryptography or certificate trust. */
#define HYPERV_TLS_TEST 1
#include "hyperv_dhcp_tests.c"
static unsigned tls_phase, no_server_reply,overlap_reply,http_reply;
static unsigned certificate_error,application_sends;
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
    assert(get_be16(p+48)==DIHOS_TCP_RECEIVE_WINDOW && DIHOS_TCP_RECEIVE_WINDOW<=16384);
    unsigned flags=p[47], header=(p[46]>>4)*4;
    unsigned bytes=get_be16(p+16)-20-header;
    if (!(flags&2) && !bytes) return 0;
    if (no_server_reply && bytes) return 0;
    unsigned reply_bytes=bytes?(http_reply?sizeof(application)+1:overlap_reply?3:1):0;
    if(bytes && http_reply){
        uint8_t *future=new_reply(54);
        put_be16(future+12,0x0800);future[14]=0x45;future[23]=6;put_be16(future+16,40);
        memcpy(future+26,p+30,4);memcpy(future+30,p+26,4);memcpy(future+34,p+36,2);memcpy(future+36,p+34,2);
        put_be32(future+38,99999);future[46]=0x50;future[47]=0x11;
    }
    uint8_t *r=new_reply(54+reply_bytes);
    put_be16(r+12,0x0800);r[14]=0x45;r[23]=6;
    put_be16(r+16,40+reply_bytes);
    memcpy(r+26,p+30,4);memcpy(r+30,p+26,4);
    memcpy(r+34,p+36,2);memcpy(r+36,p+34,2);
    put_be32(r+38,bytes ? ((overlap_reply||http_reply)?999:1001):1000);
    put_be32(r+42,get_be32(p+38)+(bytes ? bytes:1));
    r[46]=0x50;r[47]=bytes ? 0x18:0x12;
    if (bytes) { r[54]=42;if(overlap_reply){r[54]=11;r[55]=22;r[56]=42;}delay_polls=9000; }
    if(bytes && http_reply){r[54]=11;r[55]=22;memcpy(r+56,application,sizeof(application)-1);r[47]|=1;delay_polls=0;}
    return 0;
}
static unsigned mock_end_chain(const br_x509_class **ctx){(void)ctx;return certificate_error;}
static const br_x509_class mock_x509={.context_size=sizeof(br_x509_minimal_context),.end_chain=mock_end_chain};
void br_ssl_client_init_full(br_ssl_client_context *c,br_x509_minimal_context *x,
    const br_x509_trust_anchor *a,size_t n) { (void)c;assert(a && n==118);memset(x,0,sizeof(*x));x->vtable=&mock_x509; }
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
{ (void)c;assert(n);++application_sends;tls_phase=3; }
void br_ssl_engine_flush(br_ssl_engine_context *c,int f) { (void)c;(void)f; }
unsigned char *br_ssl_engine_recvrec_buf(const br_ssl_engine_context *c,size_t *n)
{ (void)c;*n=sizeof(tls_wire);return tls_wire; }
void br_ssl_engine_recvrec_ack(br_ssl_engine_context *c,size_t n)
{ assert(n==1 && tls_wire[0]==42);(*c->x509ctx)->end_chain(c->x509ctx);tls_phase=2; }
unsigned char *br_ssl_engine_recvapp_buf(const br_ssl_engine_context *c,size_t *n)
{ (void)c;*n=sizeof(application)-1;return (unsigned char *)application; }
void br_ssl_engine_recvapp_ack(br_ssl_engine_context *c,size_t n)
{ (void)c;assert(n==sizeof(application)-1);tls_phase=4; }
int main(void)
{
    memcpy(g_knet.mac,fixture_mac,6);g_knet.ip=0x0a000002;
    g_knet.mask=0xffffff00;g_knet.configured=1;
    assert(!knet_usb_request_url("https://10.0.0.1/",8192,"GET",0,0,0,0,0));
    assert(tls_phase==4 && empty_polls==9000 && now==9000);
    assert(knet_usb_tls_verified() && application_sends==1);
    assert(g_tls_x509.days==719528u+1792972800u/86400 && g_tls_x509.seconds==0);
    overlap_reply=1;empty_polls=0;
    assert(!knet_usb_request_url("https://10.0.0.1/",8192,"GET",0,0,0,0,0));
    assert(knet_usb_tls_verified() && tls_phase==4 && empty_polls==9000);
    overlap_reply=0;
    http_reply=1;uint8_t response[8192];uint32_t response_bytes;uint8_t truncated;
    assert(!knet_usb_fetch_url("http://10.0.0.1/",response,sizeof(response),&response_bytes,&truncated,0,1000));
    assert(!truncated && response_bytes==sizeof(application)-1 && !memcmp(response,application,response_bytes));http_reply=0;
    no_server_reply=1;empty_polls=0;uint64_t start=now;
    assert(knet_usb_request_url("https://10.0.0.1/",8192,"GET",0,0,0,0,0)<0);
    assert(now-start>=30000 && now-start<30100 && !br_ssl_engine_last_error(&g_tls_client.eng));
    assert(!knet_usb_tls_verified());
    assert(knet_usb_fetch_url("https://10.0.0.1/",response,sizeof(response),&response_bytes,&truncated,0,1000)==-4);
    assert(strstr(knet_usb_last_fetch_error(),"TLS handshake timeout; bytes=0 tcp=") && !knet_usb_tls_verified());
    no_server_reply=0;certificate_error=BR_ERR_X509_NOT_TRUSTED;
    unsigned sent_before=application_sends;
    assert(knet_usb_request_url("https://10.0.0.1/",8192,"GET",0,0,0,0,0)<0);
    assert(!knet_usb_tls_verified() && application_sends==sent_before);
    certificate_error=0;entropy_error=-61;
    assert(knet_usb_request_url("https://10.0.0.1/",8192,"GET",0,0,0,0,0)<0);
    assert(!knet_usb_tls_verified() && application_sends==sent_before);
    puts("PASS: transport requires root anchors, validator success, handshake completion and entropy before application bytes; state resets after failures");
    puts("PASS: production HTTPS loop waits through 9000 empty polls for delayed TLS data; silent peer reaches 30s deadline; mock engine, no cryptographic validation");
    puts("PASS: bounded TCP receive window and overlapping retransmission deliver only unseen bytes through the production HTTPS loop");
    puts("PASS: HTTP overlap recovery ignores a future FIN; kernel timeout retains TLS phase and byte diagnostics");
    return 0;
}
