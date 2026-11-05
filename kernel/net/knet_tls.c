#include "net/dihos_tls_build.h"
#include "net/knet_tls.h"
#include "net/knet_link.h"
#include "system/entropy.h"
#include "system/dihos_time.h"
#include "kwrappers/string.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include "tls_roots_pem.inc"
#include "net/dihos_tls_extra_ca.h"
#if DIHOS_TLS_EXTRA_CA_COUNT > 1
#error Only one explicitly selected extra CA is supported
#endif
#define TLS_WIRE_CAPACITY 32768u
static mbedtls_ssl_context session;
static mbedtls_ssl_config configuration;
static mbedtls_x509_crt roots;
static uint8_t incoming[TLS_WIRE_CAPACITY],outgoing[TLS_WIRE_CAPACITY];
static uint32_t incoming_bytes,outgoing_bytes,verification;
static int roots_ready,session_live,authenticated,last_error;
static int random_bytes(void* ignored,unsigned char* out,size_t bytes){(void)ignored;knet_link_checkpoint();return dihos_entropy_fill(out,bytes)?MBEDTLS_ERR_SSL_INTERNAL_ERROR:0;}
static int send_bytes(void* ignored,const unsigned char* bytes,size_t n){
    (void)ignored;knet_link_checkpoint();
    if(n>TLS_WIRE_CAPACITY-outgoing_bytes)return MBEDTLS_ERR_SSL_WANT_WRITE;
    memcpy(outgoing+outgoing_bytes,bytes,n);outgoing_bytes+=(uint32_t)n;return (int)n;
}
static int receive_bytes(void* ignored,unsigned char* bytes,size_t n){
    (void)ignored;knet_link_checkpoint();
    if(!incoming_bytes)return MBEDTLS_ERR_SSL_WANT_READ;
    if(n>incoming_bytes)n=incoming_bytes;memcpy(bytes,incoming,n);incoming_bytes-=(uint32_t)n;
    memmove(incoming,incoming+n,incoming_bytes);return (int)n;
}
void knet_tls_end(void){
    authenticated=0;
    if(session_live){mbedtls_ssl_free(&session);mbedtls_ssl_config_free(&configuration);session_live=0;}
    incoming_bytes=outgoing_bytes=0;memset(incoming,0,sizeof(incoming));memset(outgoing,0,sizeof(outgoing));
}
int knet_tls_begin(const char* host){
    knet_tls_end();last_error=0;verification=0;
    uint64_t utc;if(!host || !*host || dihos_time_utc(&utc) || !utc)return last_error=-1;
    psa_status_t status=psa_crypto_init();if(status!=PSA_SUCCESS)return last_error=(int)status;
    if(!roots_ready){
        mbedtls_x509_crt_init(&roots);int rc=mbedtls_x509_crt_parse(&roots,dihos_tls_ca_pem,sizeof(dihos_tls_ca_pem));
        unsigned count=0;for(mbedtls_x509_crt* p=&roots;p && p->raw.p;p=p->next)++count;
        if(rc || count!=DIHOS_TLS_CA_COUNT){mbedtls_x509_crt_free(&roots);return last_error=rc?rc:-1;}
#if DIHOS_TLS_EXTRA_CA_COUNT
        static const unsigned char extra_ca[]=DIHOS_TLS_EXTRA_CA_PEM;
        mbedtls_x509_crt* tail=&roots;while(tail->next)tail=tail->next;
        rc=mbedtls_x509_crt_parse(&roots,extra_ca,sizeof(extra_ca));
        mbedtls_x509_crt* added=tail->next;
        if(rc || !added || added->next || !mbedtls_x509_crt_get_ca_istrue(added) ||
           mbedtls_x509_crt_check_key_usage(added,MBEDTLS_X509_KU_KEY_CERT_SIGN)){
            mbedtls_x509_crt_free(&roots);return last_error=rc?rc:-1;
        }
#endif
        roots_ready=1;
    }
    mbedtls_ssl_init(&session);mbedtls_ssl_config_init(&configuration);session_live=1;
    int rc=mbedtls_ssl_config_defaults(&configuration,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT);
    if(rc)return last_error=rc;
    mbedtls_ssl_conf_authmode(&configuration,MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&configuration,&roots,0);mbedtls_ssl_conf_rng(&configuration,random_bytes,0);
    mbedtls_ssl_conf_min_tls_version(&configuration,MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&configuration,MBEDTLS_SSL_VERSION_TLS1_3);
    static const char* protocols[]={"http/1.1",0};
    if((rc=mbedtls_ssl_conf_alpn_protocols(&configuration,protocols)))return last_error=rc;
    mbedtls_ssl_conf_session_tickets(&configuration,MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
    mbedtls_ssl_conf_renegotiation(&configuration,MBEDTLS_SSL_RENEGOTIATION_DISABLED);
    if((rc=mbedtls_ssl_setup(&session,&configuration)) || (rc=mbedtls_ssl_set_hostname(&session,host)))return last_error=rc;
    mbedtls_ssl_set_bio(&session,0,send_bytes,receive_bytes,0);return 0;
}
int knet_tls_handshake(void){
    if(!session_live)return -1;if(authenticated)return 0;
    knet_link_checkpoint();int rc=mbedtls_ssl_handshake(&session);knet_link_checkpoint();
    verification=mbedtls_ssl_get_verify_result(&session);
    if(verification==UINT32_MAX)verification=0; /* No certificate result yet. */
    if(rc==MBEDTLS_ERR_SSL_WANT_READ || rc==MBEDTLS_ERR_SSL_WANT_WRITE)return 1;
    if(rc || verification || !mbedtls_ssl_get_peer_cert(&session)){last_error=rc?rc:-1;return -1;}
    const char* protocol=mbedtls_ssl_get_alpn_protocol(&session);
    if(protocol && strcmp(protocol,"http/1.1")){last_error=-1;return -1;}
    authenticated=1;return 0;
}
int knet_tls_authenticated(void){return authenticated;}
int knet_tls_feed(const void* bytes,uint32_t n){if(!session_live || (!bytes && n) || n>TLS_WIRE_CAPACITY-incoming_bytes)return -1;memcpy(incoming+incoming_bytes,bytes,n);incoming_bytes+=n;return 0;}
uint32_t knet_tls_input_pending(void){return incoming_bytes;}
const uint8_t* knet_tls_out(uint32_t* n){*n=outgoing_bytes;return outgoing;}
void knet_tls_out_ack(uint32_t n){if(n>outgoing_bytes){last_error=-1;return;}outgoing_bytes-=n;memmove(outgoing,outgoing+n,outgoing_bytes);}
int knet_tls_write(const void* bytes,uint32_t n){
    if(!authenticated)return -1;int rc=mbedtls_ssl_write(&session,bytes,n);
    if(rc==MBEDTLS_ERR_SSL_WANT_READ || rc==MBEDTLS_ERR_SSL_WANT_WRITE)return 0;
    if(rc<0){last_error=rc;return -1;}return rc;
}
int knet_tls_read(void* bytes,uint32_t n){
    if(!authenticated)return -1;int rc=mbedtls_ssl_read(&session,bytes,n);
    if(rc==MBEDTLS_ERR_SSL_WANT_READ || rc==MBEDTLS_ERR_SSL_WANT_WRITE || rc==MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)return 0;
    if(rc==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)return -2;
    if(rc<=0){last_error=rc;return -1;}return rc;
}
uint32_t knet_tls_verify_flags(void){return verification;}
int knet_tls_last_error(void){return last_error;}
const char* knet_tls_version(void){return authenticated?mbedtls_ssl_get_version(&session):"unverified";}
const char* knet_tls_error_description(void){
    if(verification&MBEDTLS_X509_BADCERT_NOT_TRUSTED)return "TLS certificate chain is not trusted; code ";
    if(verification&MBEDTLS_X509_BADCERT_CN_MISMATCH)return "TLS certificate hostname mismatch; code ";
    if(verification&(MBEDTLS_X509_BADCERT_EXPIRED|MBEDTLS_X509_BADCERT_FUTURE))return "TLS certificate date is invalid; code ";
    return "TLS engine failure; code ";
}
