#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system/entropy.h"
#include "bearssl_rand.h"
#include "bearssl_hash.h"
#undef assert
extern void exit(int);
static void failed(unsigned line){fprintf(stderr,"FAIL: entropy test line %u\n",line);exit(1);}
#define assert(value) ((value)?(void)0:failed(__LINE__))
static int hardware,mode;static unsigned calls;static uint64_t word;
int dihos_entropy_test_hardware(void){return hardware;}
int dihos_entropy_test_word(uint64_t *out){++calls;if(mode==1)return 0;*out=mode==2?7:++word;return 1;}
static void reset(void){dihos_entropy_test_reset();hardware=mode=0;calls=0;word=0x123456789abcd000ull;}
static boot_info boot;
static void firmware(void){memset(&boot,0,sizeof boot);boot.version=3;boot.boot_rng_flags=3;for(unsigned i=0;i<48;++i)boot.boot_rng_seed[i]=(uint8_t)i;}
static unsigned nibble(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;assert(c>='A'&&c<='F');return c-'A'+10;}
static void bytes(const char *hex,uint8_t *out,unsigned n){assert(strlen(hex)==n*2);for(unsigned i=0;i<n;++i)out[i]=(uint8_t)(nibble(hex[i*2])*16+nibble(hex[i*2+1]));}
int main(void){
    // Existing upstream BearSSL HMAC-DRBG known-answer vector.
    uint8_t seed[42],reference[32],out[256],saved[256];br_hmac_drbg_context drbg;
    bytes("009A4D6792295A7F730FC3F2B49CBC0F62E862272F01795EDF0D54DB760F156D0DAC04C0322B3A204224",seed,42);
    br_hmac_drbg_init(&drbg,&br_sha256_vtable,seed,42);
    bytes("9305A46DE7FF8EB107194DEBD3FD48AA20D5E7656CBE0EA69D2A8D4E7C67",reference,30);
    br_hmac_drbg_generate(&drbg,out,30);assert(!memcmp(out,reference,30));
    reset();memset(out,0xa5,sizeof out);memcpy(saved,out,sizeof out);
    dihos_entropy_init(0);assert(dihos_entropy_fill(out,32)==-11 && !memcmp(out,saved,sizeof out) && !calls);
    reset();firmware();boot.version=2;dihos_entropy_init(&boot);assert(dihos_entropy_fill(out,1)==-11);
    reset();firmware();boot.boot_rng_status=1;dihos_entropy_init(&boot);assert(dihos_entropy_fill(out,1)==-11 && !boot.boot_rng_flags);
    reset();firmware();memset(boot.boot_rng_seed,0,48);dihos_entropy_init(&boot);assert(dihos_entropy_fill(out,1)==-11);
    reset();firmware();dihos_entropy_init(&boot);
    for(unsigned i=0;i<48;++i)assert(!boot.boot_rng_seed[i]);assert(!boot.boot_rng_flags);
    assert(!dihos_entropy_fill(out,32));
    // Filled below from an independent HMAC/SHA-256 reference implementation.
    bytes("0ffb80875a3e9022a4941a3fa1b0d3611df14e1cf651a73ce9229b9f3ad56887",reference,32);
    assert(!memcmp(out,reference,32));
    assert(dihos_entropy_fill(out,257)==-7 && dihos_entropy_fill(0,1)==-22 && dihos_entropy_fill(out,0)==-22);
    dihos_entropy_test_requests(DIHOS_ENTROPY_REFRESH_CALLS);assert(!dihos_entropy_fill(out,1));
    dihos_entropy_test_requests(DIHOS_ENTROPY_RESEED_CALLS-1);assert(!dihos_entropy_fill(out,1));
    memset(out,0xa5,sizeof out);assert(dihos_entropy_fill(out,32)==-11);for(unsigned i=0;i<256;++i)assert(out[i]==0xa5);
    dihos_entropy_info info;dihos_entropy_status(&info);assert(!info.ready && info.requests==DIHOS_ENTROPY_RESEED_CALLS);
    reset();hardware=1;mode=1;dihos_entropy_init(0);assert(calls<=48);calls=0;assert(dihos_entropy_fill(out,1)==-11 && calls<=48);
    reset();hardware=1;mode=2;dihos_entropy_init(0);assert(dihos_entropy_fill(out,1)==-5);
    reset();hardware=1;dihos_entropy_init(0);assert(!dihos_entropy_fill(out,256));memcpy(saved,out,256);
    assert(!dihos_entropy_fill(out,256) && memcmp(saved,out,256));
    dihos_entropy_status(&info);assert(info.ready && info.sources==DIHOS_ENTROPY_RNDR && info.hardware);
    dihos_entropy_test_requests(DIHOS_ENTROPY_REFRESH_CALLS);mode=1;
    assert(!dihos_entropy_fill(out,32)); // A transient refresh failure doesn't exhaust a seeded DRBG.
    mode=0;assert(!dihos_entropy_fill(out,32));dihos_entropy_status(&info);
    assert(info.reseeds==2 && info.requests==1);
    puts("PASS: HMAC-DRBG KAT, independent seed vector, source failure, seed erasure, bounded RNDR retries, stuck source and reseed limit");
}
