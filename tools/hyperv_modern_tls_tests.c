/* Actual guest TCP/HTTPS loop, deterministic TLS adapter. Real cryptography is
 * covered separately by kernel_tls_validation_tests and the socket probe. */
#define DIHOS_TLS_MODERN 1
#define HYPERV_TLS_TEST 1
#include "hyperv_dhcp_tests.c"
static unsigned hello_sent,live,authenticated,peer,body_ready,body_offset,application_sends;
static unsigned silent,reject_peer,cancel_after,checkpoints,ended;
static int last_error;
static uint32_t verify_flags;
static uint8_t outgoing[16384],application[9200];
static char request_copy[16385];
static const char* fetch_address="https://10.0.0.1/";
static unsigned outgoing_bytes,application_bytes,request_bytes,cipher_received;
void knet_link_checkpoint(void){++checkpoints;if(cancel_after && checkpoints>=cancel_after && g_fetch_cancelled)*g_fetch_cancelled=1;}
int knet_tls_begin(const char* host){assert(host && !live);live=1;hello_sent=0;authenticated=peer=body_ready=body_offset=0;outgoing_bytes=cipher_received=0;last_error=0;verify_flags=0;if(entropy_error){last_error=-1;return -1;}return 0;}
void knet_tls_end(void){live=authenticated=0;outgoing_bytes=0;++ended;}
int knet_tls_authenticated(void){return authenticated;}
int knet_tls_handshake(void){assert(live);if(!peer){if(!hello_sent){hello_sent=1;outgoing[0]=0xa0;outgoing_bytes=1;}return 1;}if(reject_peer){verify_flags=8;last_error=-9984;return -1;}authenticated=1;return 0;}
int knet_tls_feed(const void* p,uint32_t n){assert(n==1);uint8_t b=*(const uint8_t*)p;if(b==0xb0)peer=1;else{assert(b==0xb1);body_ready=1;}return 0;}
uint32_t knet_tls_input_pending(void){return 0;}
const uint8_t* knet_tls_out(uint32_t* n){*n=outgoing_bytes;return outgoing;}
void knet_tls_out_ack(uint32_t n){assert(n<=outgoing_bytes);memmove(outgoing,outgoing+n,outgoing_bytes-n);outgoing_bytes-=n;}
int knet_tls_write(const void* p,uint32_t n){assert(authenticated && p && n<=sizeof(outgoing));memcpy(request_copy,p,n);request_copy[n]=0;request_bytes=n;memset(outgoing,0xa2,n);outgoing_bytes=n;++application_sends;return n;}
int knet_tls_read(void* p,uint32_t cap){if(!body_ready)return 0;if(body_offset==application_bytes)return -2;unsigned n=application_bytes-body_offset;if(n>cap)n=cap;memcpy(p,application+body_offset,n);body_offset+=n;return n;}
uint32_t knet_tls_verify_flags(void){return verify_flags;}
int knet_tls_last_error(void){return last_error;}
const char* knet_tls_version(void){return "TLSv1.3 (test adapter)";}
const char* knet_tls_error_description(void){return "test TLS peer rejected";}
uint64_t knet_tls_memory_pages(void){return live;}
static uint8_t* new_reply(uint32_t n){assert(reply_count<4);unsigned slot=(reply_head+reply_count++)%4;reply_lengths[slot]=n;memset(reply_frames[slot],0,n);return reply_frames[slot];}
static int tls_fixture_send(const void* data,uint32_t n){
    const uint8_t* p=data;
    if(get_be16(p+12)==0x0806){uint8_t* r=new_reply(n);memcpy(r,p,n);put_be16(r+20,2);memcpy(r+22,fixture_mac,6);memcpy(r+28,p+38,4);memcpy(r+38,p+28,4);return 0;}
    assert(p[23]==6 && n>=54 && get_be16(p+48)==16384);
    unsigned flags=p[47],header=(p[46]>>4)*4,bytes=get_be16(p+16)-20-header;
    if(!(flags&2) && !bytes)return 0;
    if(silent && bytes)return 0;
    if(bytes && p[54]==0xa2){cipher_received+=bytes;if(cipher_received<request_bytes)return 0;assert(cipher_received==request_bytes);}
    uint8_t* r=new_reply(54+(bytes?1:0));put_be16(r+12,0x0800);r[14]=0x45;r[23]=6;put_be16(r+16,40+(bytes?1:0));
    memcpy(r+26,p+30,4);memcpy(r+30,p+26,4);memcpy(r+34,p+36,2);memcpy(r+36,p+34,2);
    put_be32(r+38,bytes?(p[54]==0xa0?1001:1002):1000);put_be32(r+42,get_be32(p+38)+(bytes?bytes:1));r[46]=0x50;r[47]=bytes?0x18:0x12;
    if(bytes)r[54]=p[54]==0xa0?0xb0:0xb1;
    return 0;
}
static int fetch(volatile uint32_t* cancel){uint8_t response[10000],truncated;uint32_t bytes;int rc=knet_usb_fetch_url(fetch_address,response,sizeof(response),&bytes,&truncated,cancel,1000);if(!rc)assert(!truncated && bytes==application_bytes && !memcmp(response,application,bytes));assert(!live && !g_fetch_busy);return rc;}
int main(void){
    memcpy(g_knet.mac,fixture_mac,6);g_knet.ip=0x0a000002;g_knet.mask=0xffffff00;g_knet.configured=1;
    const char* header="HTTP/1.1 200 OK\r\nContent-Length: 9000\r\n\r\n";application_bytes=strlen(header)+9000;memcpy(application,header,strlen(header));memset(application+strlen(header),'x',9000);
    assert(!fetch(0) && application_sends==1 && knet_usb_tls_verified());
    unsigned sends=application_sends;reject_peer=1;assert(fetch(0)<0 && !knet_usb_tls_verified() && application_sends==sends);reject_peer=0;
    assert(!fetch(0));silent=1;uint64_t start=now;assert(fetch(0)==-4 && now-start>=1000 && now-start<1100 && !knet_usb_tls_verified());silent=0;
    assert(!fetch(0));volatile uint32_t cancel=0;cancel_after=checkpoints+2;assert(fetch(&cancel)==-3 && cancel);cancel_after=0;
    entropy_error=-1;assert(fetch(0)<0 && !knet_usb_tls_verified());entropy_error=0;assert(!fetch(0));
    assert(ended>=8);
    char long_url[SACX_HTTP_URL_MAX+2];memset(long_url,'a',sizeof(long_url));const char* prefix="https://10.0.0.1/search?q=";memcpy(long_url,prefix,strlen(prefix));
    const char* tail="&tail=complete";memcpy(long_url+SACX_HTTP_URL_MAX-strlen(tail),tail,strlen(tail));long_url[SACX_HTTP_URL_MAX]=0;
    fetch_address=long_url;assert(!fetch(0));assert(strstr(request_copy,"&tail=complete HTTP/1.1\r\n") && request_bytes>SACX_HTTP_URL_MAX);
    sends=application_sends;long_url[SACX_HTTP_URL_MAX]='a';long_url[SACX_HTTP_URL_MAX+1]=0;assert(fetch(0)<0 && application_sends==sends);
    puts("PASS: full 8191-byte request URL and query tail reach the transport across multiple sends; oversized URLs fail without sending a truncated request");
    puts("PASS: modern guest transport delivers multi-chunk authenticated response, rejects peer/entropy failure, times out, cancels and recovers without retained ownership; TLS adapter mocked");return 0;
}
