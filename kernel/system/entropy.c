#include "system/entropy.h"
#include "bearssl_rand.h"
#include "bearssl_hash.h"
static br_hmac_drbg_context generator;
static dihos_entropy_info state;
static uint32_t lock,initialized;
static uint64_t last_word;
static int have_last_word;
static void wipe(void *data,size_t bytes){volatile uint8_t *p=data;while(bytes--)*p++=0;}
static int varied(const uint8_t *p,size_t n){uint8_t difference=0;for(size_t i=1;i<n;++i)difference|=p[i]^p[0];return difference!=0;}
static int hardware_present(void){
#ifdef DIHOS_ENTROPY_TEST
    return dihos_entropy_test_hardware();
#elif defined(__aarch64__)
    uint64_t features;__asm__ volatile("mrs %0,id_aa64isar0_el1":"=r"(features));return ((features>>60)&15)==1;
#else
    return 0;
#endif
}
static int random_word(uint64_t *word){
#ifdef DIHOS_ENTROPY_TEST
    return dihos_entropy_test_word(word);
#elif defined(__aarch64__)
    uint64_t value;unsigned ok;
    __asm__ volatile("mrs %0,S3_3_C2_C4_0\n cset %w1,ne":"=r"(value),"=r"(ok)::"cc");
    if(ok)*word=value;return ok;
#else
    (void)word;return 0;
#endif
}
static int hardware_seed(uint8_t seed[48]){
    if(!state.hardware)return -11;
    for(unsigned i=0;i<6;++i){
        uint64_t word=0;int ok=0;
        for(unsigned attempt=0;attempt<8 && !ok;++attempt)ok=random_word(&word);
        if(!ok){wipe(seed,48);return -11;}
        // Reject an obvious stuck source; not an entropy-quality certification.
        if(have_last_word && word==last_word){wipe(seed,48);return -5;}
        last_word=word;have_last_word=1;
        for(unsigned j=0;j<8;++j)seed[i*8+j]=(uint8_t)(word>>(j*8));
        wipe(&word,sizeof(word));
    }
    return 0;
}
static int reseed_hardware(void){
    uint8_t seed[48];int result=hardware_seed(seed);
    if(!result){
        if(state.ready)br_hmac_drbg_update(&generator,seed,sizeof(seed));
        else br_hmac_drbg_init(&generator,&br_sha256_vtable,seed,sizeof(seed));
        state.ready=1;state.sources|=DIHOS_ENTROPY_RNDR;state.requests=0;++state.reseeds;
    }
    wipe(seed,sizeof(seed));return result;
}
void dihos_entropy_init(boot_info *boot){
    // Early boot only: before general allocation, IRQ callbacks or user tasks.
    if(initialized){if(boot && boot->version>=3){wipe(boot->boot_rng_seed,48);boot->boot_rng_flags=0;}return;}
    initialized=1;state.hardware=hardware_present();
    if(boot && boot->version>=3){
        state.firmware_status=boot->boot_rng_status;
        if((boot->boot_rng_flags&1) && !boot->boot_rng_status && varied(boot->boot_rng_seed,48)){
            br_hmac_drbg_init(&generator,&br_sha256_vtable,boot->boot_rng_seed,48);
            state.ready=1;state.sources=DIHOS_ENTROPY_FIRMWARE|((boot->boot_rng_flags&2)?DIHOS_ENTROPY_RAW:0);
            state.reseeds=1;
        }
        wipe(boot->boot_rng_seed,48);boot->boot_rng_flags=0;
    }
    int result=reseed_hardware();state.last_error=state.ready?0:(uint32_t)result;
}
int dihos_entropy_fill(void *output,size_t bytes){
    if(!output || !bytes)return -22;
    if(bytes>DIHOS_ENTROPY_MAX)return -7;
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&lock,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return -11;
    int result=0;
    // Opportunistic refresh is not a small exhaustible global entropy budget.
    // Enforce the HMAC-DRBG 2^48 Generate-call limit even without fresh input.
    if(state.ready && state.hardware && state.requests>=DIHOS_ENTROPY_REFRESH_CALLS &&
       state.requests<DIHOS_ENTROPY_RESEED_CALLS)(void)reseed_hardware();
    if(!state.ready || state.requests>=DIHOS_ENTROPY_RESEED_CALLS)result=reseed_hardware();
    if(!result){br_hmac_drbg_generate(&generator,output,bytes);++state.requests;}
    state.last_error=(uint32_t)result;__atomic_store_n(&lock,0,__ATOMIC_RELEASE);return result;
}
void dihos_entropy_status(dihos_entropy_info *out){
    if(!out)return;
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&lock,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
        *out=(dihos_entropy_info){0};out->last_error=(uint32_t)-11;return;
    }
    *out=state;out->ready=state.ready && state.requests<DIHOS_ENTROPY_RESEED_CALLS;
    __atomic_store_n(&lock,0,__ATOMIC_RELEASE);
}
#ifdef DIHOS_ENTROPY_TEST
void dihos_entropy_test_reset(void){wipe(&generator,sizeof(generator));wipe(&state,sizeof(state));lock=initialized=0;last_word=0;have_last_word=0;}
void dihos_entropy_test_requests(uint64_t requests){state.requests=requests;}
#endif
