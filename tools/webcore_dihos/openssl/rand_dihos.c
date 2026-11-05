/* OpenSSL seed-pool backend. Credit only bytes returned by the SACX CSPRNG. */
#include <openssl/crypto.h>
#include "crypto/rand_pool.h"
#include "crypto/rand.h"
#include <sacx_user_random.h>
int ossl_rand_pool_init(void){return 1;} /* No device handles to initialize. */
void ossl_rand_pool_cleanup(void){}
void ossl_rand_pool_keep_random_devices_open(int keep){(void)keep;}
size_t ossl_pool_acquire_entropy(RAND_POOL* pool){
    size_t needed=ossl_rand_pool_bytes_needed(pool,1);
    if(needed){
        if(needed>4096)return ossl_rand_pool_entropy_available(pool);
        unsigned char* buffer=ossl_rand_pool_add_begin(pool,needed);
        if(!buffer)return ossl_rand_pool_entropy_available(pool);
        if(sacx_user_random(buffer,needed)){
            OPENSSL_cleanse(buffer,needed);
            ossl_rand_pool_add_end(pool,0,0);
        }else ossl_rand_pool_add_end(pool,needed,needed*8);
    }
    return ossl_rand_pool_entropy_available(pool);
}
int ossl_pool_add_nonce_data(RAND_POOL* pool){
    unsigned char nonce[32];
    if(sacx_user_random(nonce,sizeof(nonce))){OPENSSL_cleanse(nonce,sizeof(nonce));return 0;}
    int result=ossl_rand_pool_add(pool,nonce,sizeof(nonce),0);
    OPENSSL_cleanse(nonce,sizeof(nonce));return result;
}
int ossl_pool_add_additional_data(RAND_POOL* pool){return ossl_pool_add_nonce_data(pool);}
