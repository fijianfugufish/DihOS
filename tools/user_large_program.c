#include "sacx_user_api.h"
#include <stddef.h>
extern void *malloc(size_t);
extern void free(void*);
extern void *realloc(void*,size_t);
static const volatile unsigned char payload[20*1024*1024] __attribute__((section(".rodata.big")))={[0]=18,[20*1024*1024-1]=91};
static const volatile void *volatile pointers[2048]={[0 ... 2047]=payload};
static volatile unsigned char zeroes[4*1024*1024];
int dihos_user_main(const sacx_api *api){
    if(payload[0]!=18 || payload[sizeof(payload)-1]!=91)sacx_user_exit(162,"Large ELF initialized payload missing");
    for(unsigned i=0;i<2048;++i)if(pointers[i]!=payload)sacx_user_exit(162,"Large ELF relative relocation failed");
#if USER_LARGE_KIND==2
    *(volatile unsigned char*)(uintptr_t)payload=1;sacx_user_exit(162,"Large ELF RO protection missing");
#endif
    for(unsigned i=0;i<sizeof(zeroes);i+=4096){if(zeroes[i])sacx_user_exit(162,"Large ELF BSS not zeroed");zeroes[i]=42;}
    void *raw=0;if(api->mem_alloc(8*1024*1024,&raw))sacx_user_exit(162,"Large raw reservation failed");
    for(unsigned i=0;i<8*1024*1024;i+=4096)if(((unsigned char*)raw)[i])sacx_user_exit(162,"Large heap commit not zeroed");
    if(api->mem_free(raw))sacx_user_exit(162,"Large raw release failed");
    unsigned char *block=malloc(8*1024*1024);if(!block)sacx_user_exit(162,"Large arena allocation failed");
    for(unsigned i=0;i<8*1024*1024;i+=4096)block[i]=(unsigned char)(i/4096);
    if(realloc(block,8*1024*1024+1))sacx_user_exit(162,"Oversized arena allocation accepted");
    for(unsigned i=0;i<8*1024*1024;i+=4096)if(block[i]!=(unsigned char)(i/4096))sacx_user_exit(162,"Large allocation data corrupted");
    free(block);
    api->log("SACX large image: 20 MiB payload, relocations, BSS and 8 MiB allocation OK");return 86;
}
