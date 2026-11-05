#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "bootinfo.h"
extern void exit(int);
static void check(int value,unsigned line){if(!value){fprintf(stderr,"FAIL: firmware RNG line %u\n",line);exit(1);}}
#define require(value) check(!!(value),__LINE__)
typedef uint64_t UINTN,EFI_STATUS;
typedef struct {uint32_t Data1;uint16_t Data2,Data3;uint8_t Data4[8];} EFI_GUID;
typedef struct {void *BootServices;} EFI_SYSTEM_TABLE;
typedef EFI_STATUS (*EFI_LOCATE_PROTOCOL)(const EFI_GUID*,void*,void**);
static EFI_LOCATE_PROTOCOL BsLocate(void *p){return *(EFI_LOCATE_PROTOCOL*)p;}
#include "../src/stage2/stage2_rng.inc"
static unsigned mode,calls;static int missing;
static EFI_STATUS get(dihos_efi_rng *self,const EFI_GUID *algorithm,UINTN size,uint8_t *out){
    (void)self;++calls;require(size==48);
    for(unsigned i=0;i<48;++i)out[i]=(uint8_t)(i+7);
    if(algorithm){require(algorithm->Data1==0xe43176d7 && algorithm->Data4[7]==0x61);
        if(mode==1 || mode==3)return 0x8000000000000003ull;
        if(mode==2)return 0x8000000000000006ull;
    }else if(mode==3)return 0x8000000000000007ull;
    return 0;
}
static dihos_efi_rng rng={0,get};
static EFI_STATUS locate(const EFI_GUID *guid,void *registration,void **out){
    require(guid->Data1==0x3152bca5 && guid->Data4[7]==0x44 && !registration);
    if(missing)return 0x800000000000000eull;*out=&rng;return 0;
}
int main(void){
    EFI_LOCATE_PROTOCOL fn=locate;EFI_SYSTEM_TABLE system={&fn};boot_info boot={0};
    stage2_capture_rng(&system,&boot);require(boot.boot_rng_flags==3 && !boot.boot_rng_status && calls==1);
    for(unsigned i=0;i<48;++i)require(boot.boot_rng_seed[i]==i+7);
    mode=1;calls=0;stage2_capture_rng(&system,&boot);require(boot.boot_rng_flags==1 && calls==2);
    mode=2;calls=0;stage2_capture_rng(&system,&boot);require(!boot.boot_rng_flags && calls==1 && boot.boot_rng_status);
    for(unsigned i=0;i<48;++i)require(!boot.boot_rng_seed[i]);
    mode=3;calls=0;stage2_capture_rng(&system,&boot);require(!boot.boot_rng_flags && calls==2 && boot.boot_rng_status);
    missing=1;stage2_capture_rng(&system,&boot);require(!boot.boot_rng_flags && boot.boot_rng_status);
    missing=0;mode=0;rng.get_rng=0;stage2_capture_rng(&system,&boot);require(!boot.boot_rng_flags && boot.boot_rng_status);
    system.BootServices=0;stage2_capture_rng(&system,&boot);require(!boot.boot_rng_flags);
    require(offsetof(boot_info,boot_rng_seed)>offsetof(boot_info,boot_utc_seconds));
    puts("PASS: firmware RNG ABI, raw/default fallback, NOT_READY, missing/malformed provider and seed clearing");
}
