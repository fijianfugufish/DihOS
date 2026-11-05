#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "process/dihos_user_elf.h"
static uint64_t get(const unsigned char *p){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=(uint64_t)p[i]<<(8*i);return v;}
static void put(unsigned char *p,uint64_t v){for(unsigned i=0;i<8;++i)p[i]=(unsigned char)(v>>(8*i));}
int main(int argc,char **argv){
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);assert(!fseek(file,0,SEEK_END));long length=ftell(file);assert(length>20*1024*1024);rewind(file);
    unsigned char *source=malloc((size_t)length),*copy=malloc((size_t)length);assert(source && copy);
    assert(fread(source,1,(size_t)length,file)==(size_t)length);fclose(file);memcpy(copy,source,(size_t)length);
    dihos_user_elf_plan plan;assert(sizeof(plan)<8192);
    assert(!dihos_user_elf_validate(copy,(uint64_t)length,&plan));
    assert(plan.bytes>24*1024*1024 && plan.relocation_count>256);
    uint64_t ph=get(copy+32);unsigned loads=0;
    for(unsigned i=0;i<(unsigned)(copy[56]|copy[57]<<8);++i){
        unsigned char *h=copy+ph+(uint64_t)i*56;
        if(h[0]!=1 || h[1] || h[2] || h[3])continue;
        uint64_t saved_size=get(h+40),address=get(h+16);
        put(h+40,DIHOS_USER_ELF_MAX_IMAGE_BYTES-address+1ull);
        dihos_user_elf_plan rejected;
        assert(dihos_user_elf_validate(copy,(uint64_t)length,&rejected)<0 && !rejected.count);
        put(h+40,saved_size);++loads;
    }
    assert(loads);
    assert(!dihos_user_elf_headers(copy,(uint64_t)length,&plan));
    for(unsigned i=0;i<plan.relocation_count;i+=256){unsigned n=plan.relocation_count-i;if(n>256)n=256;assert(!dihos_user_elf_validate_step(&plan,i,n));}
    assert(dihos_user_elf_validate_step(&plan,0,257)<0);
    unsigned char *image=malloc((size_t)plan.bytes),*saved=malloc((size_t)plan.bytes);assert(image && saved);
    memset(image,0xa5,(size_t)plan.bytes);memcpy(saved,image,(size_t)plan.bytes);
    uint64_t at=plan.relocation_offset+256*24;
    put(copy+at,get(copy+at-24));
    assert(dihos_user_elf_validate_step(&plan,256,1)<0);
    assert(dihos_user_elf_relocate(&plan,image,plan.bytes)<0 && !memcmp(image,saved,(size_t)plan.bytes));
    memcpy(copy,source,(size_t)length);assert(!dihos_user_elf_headers(copy,(uint64_t)length,&plan));
    at=plan.relocation_offset+(plan.relocation_count-1)*24;
    put(copy+at+8,1026);assert(dihos_user_elf_validate_step(&plan,plan.relocation_count-1,1)<0);
    assert(dihos_user_elf_relocate(&plan,image,plan.bytes)<0 && !memcmp(image,saved,(size_t)plan.bytes));
    memcpy(copy,source,(size_t)length);assert(!dihos_user_elf_validate(copy,(uint64_t)length,&plan));
    dihos_user_elf_plan invalid=plan;invalid.relocation_offset=UINT64_MAX;
    assert(dihos_user_elf_relocate_step(&invalid,image,plan.bytes,0,1)<0);
    assert(dihos_user_elf_validate(copy,DIHOS_USER_ELF_MAX_BYTES+1ull,&invalid)<0);
    unsigned char *padded=calloc(1,DIHOS_USER_ELF_MAX_BYTES+1ull);assert(padded);
    memcpy(padded,source,(size_t)length);
    assert(!dihos_user_elf_validate(padded,DIHOS_USER_ELF_MAX_BYTES,&invalid));
    assert(dihos_user_elf_validate(padded,DIHOS_USER_ELF_MAX_BYTES+1ull,&invalid)<0);
    free(padded);
    assert(dihos_user_elf_relocate_step(&plan,image,plan.bytes,0,257)<0);
    assert(!dihos_user_elf_relocate(&plan,image,plan.bytes));
    for(unsigned i=0;i<plan.relocation_count;++i){const unsigned char *r=copy+plan.relocation_offset+(uint64_t)i*24;assert(get(image+get(r))==(uintptr_t)image+get(r+16));}
    free(image);free(saved);free(source);free(copy);
    puts("PASS: large ELF, bounded file/image ceilings, compact indexed plan, batched validation, cross-batch duplicate/type/bounds rejection and atomic relocation failure");
}
