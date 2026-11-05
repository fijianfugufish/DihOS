// Native OpenSSL integration. Vectors: upstream 30-test_evp_data
// (SHA-256, RFC 4231 HMAC, NIST AES-GCM, RFC 8032 Ed25519).
#include <sacx_user_api.h>
#include <sacx_user_threads.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <string.h>
static const sacx_api* diagnosticApi;
static void check(bool ok,const char* text){if(!ok){
    if(diagnosticApi){for(unsigned i=0;i<8;++i){unsigned long code=ERR_get_error();if(!code)break;char message[240];ERR_error_string_n(code,message,sizeof(message));diagnosticApi->log(message);}}
    sacx_user_exit(181,text);
}}
static unsigned nibble(char c){return c>='a'?c-'a'+10:c-'0';}
static bool equalsHex(const unsigned char* data,size_t size,const char* hex){
    if(strlen(hex)!=size*2)return false;
    for(size_t i=0;i<size;++i)if(data[i]!=((nibble(hex[2*i])<<4)|nibble(hex[2*i+1])))return false;
    return true;
}
static void decode(unsigned char* data,size_t size,const char* hex){check(strlen(hex)==size*2,"OpenSSL: vector length");for(size_t i=0;i<size;++i)data[i]=(nibble(hex[i*2])<<4)|nibble(hex[i*2+1]);}
static void digestTests(){
    unsigned char hash[32];unsigned size=0;
    check(EVP_Digest("abc",3,hash,&size,EVP_sha256(),nullptr)==1,"OpenSSL: SHA256 execution");
    check(size==32 && equalsHex(hash,32,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),"OpenSSL: SHA256 known answer");
    unsigned char key[20];memset(key,0x0b,sizeof(key));
    check(HMAC(EVP_sha256(),key,sizeof(key),reinterpret_cast<const unsigned char*>("Hi There"),8,hash,&size) && size==32 && equalsHex(hash,32,"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"),"OpenSSL: HMAC known answer");
}
static void cipherTests(){
    unsigned char key[16]{},iv[12]{},plain[16]{},cipher[32]{},tag[16]{},output[32]{};int count=0,final=0;
    auto* ctx=EVP_CIPHER_CTX_new();check(ctx,"OpenSSL: cipher allocation");
    check(EVP_EncryptInit_ex(ctx,EVP_aes_128_gcm(),nullptr,key,iv)==1 && EVP_EncryptUpdate(ctx,cipher,&count,plain,16)==1 && count==16 && EVP_EncryptFinal_ex(ctx,cipher+count,&final)==1 && final==0 && EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_GET_TAG,16,tag)==1,"OpenSSL: GCM encrypt");
    check(equalsHex(cipher,16,"0388dace60b6a392f328c2b971b2fe78") && equalsHex(tag,16,"ab6e47d42cec13bdf53a67b21257bddf"),"OpenSSL: AES GCM known answer");
    check(EVP_DecryptInit_ex(ctx,EVP_aes_128_gcm(),nullptr,key,iv)==1 && EVP_DecryptUpdate(ctx,output,&count,cipher,16)==1 && EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_SET_TAG,16,tag)==1 && EVP_DecryptFinal_ex(ctx,output+count,&final)==1 && !memcmp(output,plain,16),"OpenSSL: GCM authenticated decrypt");
    tag[0]^=1;
    check(EVP_DecryptInit_ex(ctx,EVP_aes_128_gcm(),nullptr,key,iv)==1 && EVP_DecryptUpdate(ctx,output,&count,cipher,16)==1 && EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_SET_TAG,16,tag)==1 && EVP_DecryptFinal_ex(ctx,output+count,&final)!=1,"OpenSSL: reject modified authentication tag");
    EVP_CIPHER_CTX_free(ctx);
}
static void signatureTests(){
    unsigned char publicKey[32],signature[64];
    decode(publicKey,32,"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    decode(signature,64,"e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    auto* key=EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519,nullptr,publicKey,sizeof(publicKey));
    auto* ctx=EVP_MD_CTX_new();check(key && ctx,"OpenSSL: signature allocation");
    check(EVP_DigestVerifyInit(ctx,nullptr,nullptr,nullptr,key)==1 && EVP_DigestVerify(ctx,signature,64,reinterpret_cast<const unsigned char*>(""),0)==1,"OpenSSL: Ed25519 known answer");
    signature[0]^=1;
    check(EVP_DigestVerify(ctx,signature,64,reinterpret_cast<const unsigned char*>(""),0)!=1,"OpenSSL: reject modified signature");
    EVP_MD_CTX_free(ctx);EVP_PKEY_free(key);
}
static CRYPTO_ONCE once=CRYPTO_ONCE_STATIC_INIT;
static CRYPTO_THREAD_LOCAL key;
static unsigned onceCalls=0,destructors=0;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gateCondition=PTHREAD_COND_INITIALIZER;
static unsigned arrived=0;
static bool released=false;
static void initializeOnce(){__atomic_fetch_add(&onceCalls,1,__ATOMIC_RELAXED);}
static void destroyKey(void* p){if(p)__atomic_fetch_add(&destructors,1,__ATOMIC_RELAXED);}
static uint64_t worker(uint64_t id){
    check(CRYPTO_THREAD_run_once(&once,initializeOnce)==1,"OpenSSL: concurrent once");
    check(CRYPTO_THREAD_set_local(&key,reinterpret_cast<void*>(id))==1,"OpenSSL: thread key set");
    check(!pthread_mutex_lock(&gate),"OpenSSL: worker gate lock");
    ++arrived;check(!pthread_cond_broadcast(&gateCondition),"OpenSSL: gate arrival notify");
    while(!released)check(!pthread_cond_wait(&gateCondition,&gate),"OpenSSL: blocked worker condition");
    check(!pthread_mutex_unlock(&gate),"OpenSSL: worker gate unlock");
    for(unsigned i=0;i<8;++i){digestTests();check(CRYPTO_THREAD_get_local(&key)==reinterpret_cast<void*>(id),"OpenSSL: isolated thread keys");}
    OPENSSL_thread_stop();return id+10;
}
extern "C" int dihos_user_main(const sacx_api* api){
    diagnosticApi=api;
    check(OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG,nullptr)==1,"OpenSSL: native initialization");
#ifdef DIHOS_CRYPTO_UNREADY
    unsigned char output[32];memset(output,0x5a,sizeof(output));
    check(RAND_bytes(output,sizeof(output))!=1,"OpenSSL: reject unavailable trusted entropy");
    OPENSSL_cleanse(output,sizeof(output));OPENSSL_cleanup();
    api->log("SACX OpenSSL: unavailable entropy fails closed OK");return 117;
#endif
    digestTests();cipherTests();signatureTests();api->log("OpenSSL native: hash, MAC, cipher and signature vectors OK");
    unsigned char first[32],second[32];
    check(RAND_bytes(first,32)==1 && RAND_bytes(second,32)==1 && memcmp(first,second,32),"OpenSSL: native entropy and DRBG");
    check(RAND_load_file("/not-authorized/seed",32)<=0,"OpenSSL: seed files unavailable");
    check(CRYPTO_THREAD_init_local(&key,destroyKey)==1,"OpenSSL: native thread key allocation");
    int64_t a=sacx_user_thread_create(worker,1),b=sacx_user_thread_create(worker,2);check(a>0 && b>0,"OpenSSL: worker creation");
    check(!pthread_mutex_lock(&gate),"OpenSSL: main gate lock");
    while(arrived<2)check(!pthread_cond_wait(&gateCondition,&gate),"OpenSSL: arrival condition");
    released=true;check(!pthread_cond_broadcast(&gateCondition) && !pthread_mutex_unlock(&gate),"OpenSSL: release worker conditions");
    uint64_t result=0;check(!sacx_user_thread_join(a,&result,60000) && result==11,"OpenSSL: first worker join");
    check(!sacx_user_thread_join(b,&result,60000) && result==12,"OpenSSL: second worker join");
    check(onceCalls==1 && destructors==2 && !CRYPTO_THREAD_get_local(&key),"OpenSSL: once and TLS destructor ownership");
    check(CRYPTO_THREAD_cleanup_local(&key)==1,"OpenSSL: thread key release");
    check(!pthread_cond_destroy(&gateCondition) && !pthread_mutex_destroy(&gate),"OpenSSL: condition and mutex release");
    OPENSSL_cleanup();api->log("SACX OpenSSL: known answers, rejection, entropy and threaded cleanup OK");return 116;
}
