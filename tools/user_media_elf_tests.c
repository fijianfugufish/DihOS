/* A dense valid image exercises the new relocation ceiling, not just a
 * fabricated DT_RELASZ. The existing malformed/duplicate tests still apply. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "process/dihos_user_elf.h"
static void put(unsigned char* p,uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)p[i]=(unsigned char)(v>>(8*i));}
static unsigned char* make_image(unsigned count,size_t* length){
    uint64_t rela=4096+128,targets=rela+(uint64_t)count*24;
    *length=(size_t)((targets+(uint64_t)count*8+4095)&~4095ull);
    unsigned char* p=calloc(1,*length);assert(p);
    memcpy(p,"\177ELF\2\1\1",7);put(p+16,3,2);put(p+18,183,2);put(p+20,1,4);
    put(p+24,512,8);put(p+32,64,8);put(p+52,64,2);put(p+54,56,2);put(p+56,3,2);
    unsigned char* h=p+64;put(h,1,4);put(h+4,5,4);put(h+32,4096,8);put(h+40,4096,8);put(h+48,4096,8);
    h+=56;put(h,1,4);put(h+4,6,4);put(h+8,4096,8);put(h+16,4096,8);
    put(h+32,*length-4096,8);put(h+40,*length-4096,8);put(h+48,4096,8);
    h+=56;put(h,2,4);put(h+4,6,4);put(h+8,4096,8);put(h+16,4096,8);put(h+32,80,8);put(h+40,80,8);put(h+48,8,8);
    uint64_t tags[]={7,8,9,0x6ffffff9,0},values[]={rela,(uint64_t)count*24,24,count,0};
    for(unsigned i=0;i<5;++i){put(p+4096+i*16,tags[i],8);put(p+4104+i*16,values[i],8);}
    for(unsigned i=0;i<count;++i){put(p+rela+(uint64_t)i*24,targets+(uint64_t)i*8,8);put(p+rela+(uint64_t)i*24+8,1027,8);put(p+rela+(uint64_t)i*24+16,512,8);}
    return p;
}
int main(void){
    assert(sizeof(dihos_user_elf_plan)<8192);
    size_t length;unsigned char* bytes=make_image(DIHOS_USER_ELF_MAX_RELOCATIONS,&length);
    dihos_user_elf_plan p;assert(!dihos_user_elf_validate(bytes,length,&p));
    assert(p.relocation_count==DIHOS_USER_ELF_MAX_RELOCATIONS && p.relocation_count>262144);
    for(unsigned at=0;at<p.relocation_count;at+=256){unsigned n=p.relocation_count-at;if(n>256)n=256;assert(!dihos_user_elf_validate_step(&p,at,n));}
    assert(dihos_user_elf_validate_step(&p,0,257)<0);
    uint64_t last=p.relocation_offset+(uint64_t)(p.relocation_count-1)*24;
    put(bytes+last+8,1026,8);assert(dihos_user_elf_validate_step(&p,p.relocation_count-1,1)<0);
    put(bytes+last+8,1027,8);assert(!dihos_user_elf_validate_step(&p,p.relocation_count-1,1));
    assert(dihos_user_elf_validate(bytes,DIHOS_USER_ELF_MAX_BYTES+1ull,&p)<0);
    free(bytes);bytes=make_image(DIHOS_USER_ELF_MAX_RELOCATIONS+1,&length);
    assert(dihos_user_elf_validate(bytes,length,&p)<0 && !p.count);free(bytes);
    puts("PASS: dense 524288-fixup ELF ceiling, 256-record steps, malformed final relocation and over-limit rejection");
}
