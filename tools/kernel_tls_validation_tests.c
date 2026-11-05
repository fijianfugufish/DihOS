/* Real cryptography and native platform hooks; no Internet or guest CA edits. */
#include "net/dihos_tls_build.h"
#include "net/knet_tls.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include "kwrappers/string.h"
#include "tls_validation_fixtures.inc"
#include <stdlib.h>
extern int printf(const char*,...);
extern void exit(int);
static uint64_t utc=FIXTURE_VALID_TIME,pages;
static int clock_failure,entropy_failure,fail_after=-1;
void* pmem_alloc_pages(uint64_t n){if(fail_after==0)return 0;if(fail_after>0)--fail_after;void* p=malloc(n*4096);if(p)pages+=n;return p;}
void pmem_free_pages(void* p,uint64_t n){pages-=n;free(p);}
void knet_link_checkpoint(void){}
int dihos_time_utc(uint64_t* out){*out=utc;return clock_failure?-1:0;}
uint64_t dihos_time_monotonic_ms(void){return 1000;}
/* Test-only deterministic input; live probes use OS entropy, the guest uses
 * dihos_entropy_fill from its existing CSPRNG. */
int dihos_entropy_fill(void* out,size_t n){static unsigned value=1;if(entropy_failure)return -1;for(size_t i=0;i<n;++i)((uint8_t*)out)[i]=(uint8_t)(value++);return 0;}
static void check(int yes,const char* what){if(!yes){printf("FAIL: %s pages=%llu\n",what,(unsigned long long)pages);exit(1);}}
static int verify(const unsigned char* leaf,size_t bytes,int root_mode,const char* name,uint32_t* flags){
    mbedtls_x509_crt chain,root;mbedtls_x509_crt_init(&chain);mbedtls_x509_crt_init(&root);
    check(!mbedtls_x509_crt_parse_der(&chain,leaf,bytes),"parse signed leaf");
    if(leaf==FIXTURE_RSA_LEAF || bytes==sizeof(FIXTURE_RSA_LEAF))check(!mbedtls_x509_crt_parse_der(&chain,FIXTURE_INTERMEDIATE,sizeof(FIXTURE_INTERMEDIATE)),"parse intermediate");
    if(root_mode)check(!mbedtls_x509_crt_parse_der(&root,root_mode==1?FIXTURE_RSA_ROOT:FIXTURE_EC_ROOT,root_mode==1?sizeof(FIXTURE_RSA_ROOT):sizeof(FIXTURE_EC_ROOT)),"parse root");
    int rc=mbedtls_x509_crt_verify(&chain,root_mode?&root:0,0,name,flags,0,0);
    mbedtls_x509_crt_free(&chain);mbedtls_x509_crt_free(&root);return rc;
}
int main(void){
    check(psa_crypto_init()==PSA_SUCCESS,"PSA initialization");uint32_t flags;
    check(!verify(FIXTURE_RSA_LEAF,sizeof(FIXTURE_RSA_LEAF),1,"fixture.dihos.test",&flags) && !flags,"valid RSA/intermediate");
    check(!verify(FIXTURE_EC_LEAF,sizeof(FIXTURE_EC_LEAF),2,"fixture.dihos.test",&flags) && !flags,"valid ECDSA");
    check(verify(FIXTURE_RSA_LEAF,sizeof(FIXTURE_RSA_LEAF),1,"wrong.dihos.test",&flags) && (flags&MBEDTLS_X509_BADCERT_CN_MISMATCH),"wrong host rejection");
    utc=FIXTURE_EXPIRED_TIME;check(verify(FIXTURE_RSA_LEAF,sizeof(FIXTURE_RSA_LEAF),1,"fixture.dihos.test",&flags) && (flags&MBEDTLS_X509_BADCERT_EXPIRED),"expiry rejection");
    utc=FIXTURE_FUTURE_TIME;check(verify(FIXTURE_RSA_LEAF,sizeof(FIXTURE_RSA_LEAF),1,"fixture.dihos.test",&flags) && (flags&MBEDTLS_X509_BADCERT_FUTURE),"future date rejection");utc=FIXTURE_VALID_TIME;
    check(verify(FIXTURE_RSA_LEAF,sizeof(FIXTURE_RSA_LEAF),0,"fixture.dihos.test",&flags) && (flags&MBEDTLS_X509_BADCERT_NOT_TRUSTED),"missing anchor rejection");
    check(verify(FIXTURE_RSA_LEAF,sizeof(FIXTURE_RSA_LEAF),2,"fixture.dihos.test",&flags) && (flags&MBEDTLS_X509_BADCERT_NOT_TRUSTED),"unknown anchor rejection");
    unsigned char bad[sizeof(FIXTURE_RSA_LEAF)];memcpy(bad,FIXTURE_RSA_LEAF,sizeof(bad));bad[sizeof(bad)-1]^=1;
    check(verify(bad,sizeof(bad),1,"fixture.dihos.test",&flags) && flags,"signature alteration rejection");
    mbedtls_x509_crt malformed;mbedtls_x509_crt_init(&malformed);unsigned char broken[]={0x30,0x82,0xff,0xff,0};
    check(mbedtls_x509_crt_parse_der(&malformed,broken,sizeof(broken))<0,"malformed DER rejection");mbedtls_x509_crt_free(&malformed);
    printf("PASS: native TLS profile validates RSA/ECDSA chains and rejects hostname, expiry, future date, missing/unknown anchors, altered signatures and malformed DER\n");
    check(!knet_tls_begin("www.google.com"),"initialize production root service");check(knet_tls_handshake()==1 && !knet_tls_authenticated(),"handshake needs peer proof");
    uint32_t before,after;knet_tls_out(&before);check(knet_tls_write("SECRET",6)<0,"application gate before peer authentication");knet_tls_out(&after);check(before==after,"no application bytes queued before authentication");knet_tls_end();
    for(unsigned i=0;i<3;++i){check(!knet_tls_begin("www.google.com"),"prime cancellation");check(knet_tls_handshake()==1,"prime client hello");knet_tls_end();}
    uint64_t baseline=knet_tls_memory_pages();
    for(unsigned i=0;i<100;++i){check(!knet_tls_begin("www.google.com"),"repeat cancellation begin");check(knet_tls_handshake()==1,"repeat client hello");knet_tls_end();check(knet_tls_memory_pages()==baseline,"cancelled handshake leak");}
    for(int n=0;n<16;++n){fail_after=n;int begin=knet_tls_begin("www.google.com");if(!begin)(void)knet_tls_handshake();fail_after=-1;knet_tls_end();check(knet_tls_memory_pages()==baseline,"allocation failure cleanup");}
    clock_failure=1;check(knet_tls_begin("www.google.com")<0 && !knet_tls_authenticated(),"missing clock rejection");clock_failure=0;knet_tls_end();
    entropy_failure=1;int rc=knet_tls_begin("www.google.com");check(rc<0 || knet_tls_handshake()<0,"missing entropy rejection");check(!knet_tls_authenticated() && knet_tls_write("SECRET",6)<0,"entropy failure application gate");entropy_failure=0;knet_tls_end();
    check(knet_tls_memory_pages()==baseline && pages==baseline,"platform failure cleanup");
    printf("PASS: native TLS application gate, 100 cancelled handshakes, 16 OOM positions, clock/entropy failure and stable retained root service (%llu pages)\n",(unsigned long long)baseline);
    return 0;
}
