#pragma once
#include "bearssl.h"
/* Observe the real validator's result without suppressing any error. This is
 * not handshake completion: the transport must also observe SENDAPP/Finished. */
typedef struct dihos_tls_verifier {
    const br_x509_class *vtable;
    const br_x509_class **inner;
    unsigned checked,verified,error;
} dihos_tls_verifier;
static void dihos_tls_start_chain(const br_x509_class **ctx,const char *name){
    dihos_tls_verifier *v=(dihos_tls_verifier*)ctx;v->checked=v->verified=v->error=0;
    (*v->inner)->start_chain(v->inner,name);
}
static void dihos_tls_start_cert(const br_x509_class **ctx,uint32_t n){dihos_tls_verifier *v=(dihos_tls_verifier*)ctx;(*v->inner)->start_cert(v->inner,n);}
static void dihos_tls_append(const br_x509_class **ctx,const unsigned char *p,size_t n){dihos_tls_verifier *v=(dihos_tls_verifier*)ctx;(*v->inner)->append(v->inner,p,n);}
static void dihos_tls_end_cert(const br_x509_class **ctx){dihos_tls_verifier *v=(dihos_tls_verifier*)ctx;(*v->inner)->end_cert(v->inner);}
static unsigned dihos_tls_end_chain(const br_x509_class **ctx){
    dihos_tls_verifier *v=(dihos_tls_verifier*)ctx;v->error=(*v->inner)->end_chain(v->inner);
    v->checked=1;v->verified=!v->error;return v->error;
}
static const br_x509_pkey *dihos_tls_get_pkey(const br_x509_class *const *ctx,unsigned *usages){const dihos_tls_verifier *v=(const dihos_tls_verifier*)ctx;return (*v->inner)->get_pkey(v->inner,usages);}
static const br_x509_class dihos_tls_verifier_vtable={sizeof(dihos_tls_verifier),dihos_tls_start_chain,dihos_tls_start_cert,dihos_tls_append,dihos_tls_end_cert,dihos_tls_end_chain,dihos_tls_get_pkey};
static inline void dihos_tls_verifier_init(dihos_tls_verifier *v,const br_x509_class **inner){
    v->vtable=&dihos_tls_verifier_vtable;v->inner=inner;v->checked=v->verified=v->error=0;
}
