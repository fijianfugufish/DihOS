#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net/tls_verify.h"
#include "../kernel/net/tls_roots.inc"
#include "fixtures/tls/test-roots.inc"
typedef struct {unsigned char *bytes;size_t length;} certificate;
static certificate read_cert(const char *directory,const char *name){
    char path[2048];assert(snprintf(path,sizeof(path),"%s/%s.der",directory,name)<sizeof(path));
    FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);assert(n>0 && n<8192);
    certificate c={malloc((size_t)n),(size_t)n};assert(c.bytes && fread(c.bytes,1,c.length,f)==c.length);fclose(f);return c;
}
static unsigned verify(const br_x509_trust_anchor *roots,size_t count,const char *host,uint64_t time,certificate *chain,unsigned length){
    br_x509_minimal_context x;dihos_tls_verifier v;
    br_x509_minimal_init_full(&x,roots,count);br_x509_minimal_set_time(&x,719528u+(uint32_t)(time/86400),(uint32_t)(time%86400));
    dihos_tls_verifier_init(&v,&x.vtable);assert(!v.checked && !v.verified);
    const br_x509_class **ctx=&v.vtable;(*ctx)->start_chain(ctx,host);
    for(unsigned i=0;i<length;++i){(*ctx)->start_cert(ctx,(uint32_t)chain[i].length);
        for(size_t at=0;at<chain[i].length;){size_t n=chain[i].length-at;if(n>17)n=17;(*ctx)->append(ctx,chain[i].bytes+at,n);at+=n;}
        (*ctx)->end_cert(ctx);
    }
    unsigned error=(*ctx)->end_chain(ctx);assert(v.checked && v.error==error && v.verified==!error);return error;
}
int main(int argc,char **argv){
    assert(argc==5);uint64_t valid=strtoull(argv[2],0,10),expired=strtoull(argv[3],0,10),early=strtoull(argv[4],0,10);
    certificate chain[]={read_cert(argv[1],"rsa-leaf"),read_cert(argv[1],"intermediate")};
    certificate ec=read_cert(argv[1],"ec-leaf");
    assert(!verify(FIXTURE_TAs,FIXTURE_TAs_NUM,"fixture.dihos.test",valid,chain,2));
    assert(!verify(FIXTURE_TAs,FIXTURE_TAs_NUM,"fixture.dihos.test",valid,&ec,1));
    assert(verify(FIXTURE_TAs,FIXTURE_TAs_NUM,"wrong.dihos.test",valid,chain,2)==BR_ERR_X509_BAD_SERVER_NAME);
    assert(verify(FIXTURE_TAs,FIXTURE_TAs_NUM,"fixture.dihos.test",expired,chain,2)==BR_ERR_X509_EXPIRED);
    assert(verify(FIXTURE_TAs,FIXTURE_TAs_NUM,"fixture.dihos.test",early,chain,2)==BR_ERR_X509_EXPIRED);
    assert(verify(0,0,"fixture.dihos.test",valid,chain,2)==BR_ERR_X509_NOT_TRUSTED);
    assert(verify(DIHOS_TLS_ROOTS,DIHOS_TLS_ROOTS_COUNT,"fixture.dihos.test",valid,chain,2)==BR_ERR_X509_NOT_TRUSTED);
    chain[0].bytes[chain[0].length-1]^=1;
    assert(verify(FIXTURE_TAs,FIXTURE_TAs_NUM,"fixture.dihos.test",valid,chain,2)!=0);
    chain[0].bytes[chain[0].length-1]^=1;--chain[0].length;
    assert(verify(FIXTURE_TAs,FIXTURE_TAs_NUM,"fixture.dihos.test",valid,chain,2)!=0);
    for(unsigned i=0;i<2;++i)free(chain[i].bytes);free(ec.bytes);
    printf("PASS: real BearSSL RSA/intermediate and ECDSA chains; wrong host, expiry, future date, missing/unknown roots, altered signatures and malformed DER rejected; %u production roots exclude test CAs\n",DIHOS_TLS_ROOTS_COUNT);
}
