#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "process/dihos_user_elf.h"
#include "user_program.inc"
static void put(unsigned char *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)p[i]=(uint8_t)(v>>(i*8));}
static uint64_t get(const unsigned char *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;++i)v|=(uint64_t)p[i]<<(i*8);return v;}
static uint64_t offset_for(const unsigned char *p,uint64_t address){
    uint64_t ph=get(p+32,8);
    for(unsigned i=0;i<get(p+56,2);++i){const unsigned char *h=p+ph+i*56;
        if(get(h,4)==1 && address>=get(h+16,8) && address-get(h+16,8)<get(h+32,8))return get(h+8,8)+address-get(h+16,8);
    }
    abort();
}
int main(void){
    dihos_user_elf_plan plan;
    assert(!dihos_user_elf_validate(user_exit_elf,sizeof(user_exit_elf),&plan));assert(plan.count==2 && plan.bytes>=8192);
    for(unsigned n=0;n<64;++n)assert(dihos_user_elf_validate(user_exit_elf,n,&plan)<0 && !plan.count);
    unsigned char *p=malloc(sizeof(user_exit_elf));assert(p);uint64_t ph=get(user_exit_elf+32,8);
    const struct {unsigned offset,bytes;uint64_t value;} cases[]={
        {0,1,0},{4,1,1},{5,1,2},{16,2,2},{18,2,62},{32,8,UINT64_MAX},{54,2,1},{56,2,17},
        {24,8,1},{24,8,4096},
        {(unsigned)ph,4,2},{(unsigned)ph+4,4,7},{(unsigned)ph+8,8,UINT64_MAX},
        {(unsigned)ph+16,8,4095},{(unsigned)ph+32,8,UINT64_MAX},{(unsigned)ph+40,8,UINT64_MAX},
        {(unsigned)ph+48,8,3},{(unsigned)ph+56+16,8,0},{40,8,UINT64_MAX}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i){memcpy(p,user_exit_elf,sizeof(user_exit_elf));put(p+cases[i].offset,cases[i].value,cases[i].bytes);
        assert(dihos_user_elf_validate(p,sizeof(user_exit_elf),&plan)<0 && !plan.count);
    }
    for(unsigned i=0;i<10000;++i){memcpy(p,user_exit_elf,sizeof(user_exit_elf));p[(i*7919u)%sizeof(user_exit_elf)]^=(uint8_t)(i|1);(void)dihos_user_elf_validate(p,sizeof(user_exit_elf),&plan);}
    free(p);
    assert(!dihos_user_elf_validate(user_cpp_elf,sizeof(user_cpp_elf),&plan) && plan.relocation_count>=9 && plan.count==3);
    unsigned cpp_relocations=plan.relocation_count;
    unsigned char *q=malloc(sizeof(user_cpp_elf));assert(q);
    ph=get(user_cpp_elf+32,8);uint64_t dynamic_header=0,dynamic=0,dynamic_bytes=0,rela=0,relaent=0,relasz=0,relacount=0;
    for(unsigned i=0;i<get(user_cpp_elf+56,2);++i)if(get(user_cpp_elf+ph+i*56,4)==2){
        dynamic_header=ph+i*56;dynamic=get(user_cpp_elf+dynamic_header+8,8);dynamic_bytes=get(user_cpp_elf+dynamic_header+32,8);
    }
    assert(dynamic_header && dynamic_bytes>=16);
    for(uint64_t i=0;i<dynamic_bytes;i+=16){uint64_t tag=get(user_cpp_elf+dynamic+i,8);
        if(tag==7)rela=offset_for(user_cpp_elf,get(user_cpp_elf+dynamic+i+8,8));
        if(tag==8)relasz=dynamic+i+8;if(tag==9)relaent=dynamic+i+8;if(tag==0x6ffffff9)relacount=dynamic+i+8;
    }
    assert(rela && relasz && relaent && relacount);
    struct {uint64_t at,value;unsigned bytes;} bad[]={
        {dynamic,1,8},{dynamic,35,8},{dynamic_header,3,4},{dynamic_header,7,4},
        {rela+8,1026,8},{rela+8,(1ull<<32)|1027,8},
        {rela,get(user_cpp_elf+rela,8)+1,8},{rela,0,8},{rela,plan.bytes,8},
        {rela+16,UINT64_MAX,8},{rela+16,plan.bytes,8},
        {rela+24,get(user_cpp_elf+rela,8),8},{relaent,16,8},{relasz,cpp_relocations*24+1,8},
        {relasz,(DIHOS_USER_ELF_MAX_RELOCATIONS+1)*24,8},{relacount,cpp_relocations-1,8},
        {dynamic+16,get(user_cpp_elf+dynamic,8),8},
        {dynamic_header+16,0,8},{dynamic_header+32,15,8}
    };
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i){
        memcpy(q,user_cpp_elf,sizeof(user_cpp_elf));put(q+bad[i].at,bad[i].value,bad[i].bytes);
        assert(dihos_user_elf_validate(q,sizeof(user_cpp_elf),&plan)<0 && !plan.count && !plan.relocation_count);
    }
    memcpy(q,user_cpp_elf,sizeof(user_cpp_elf));put(q+40,0,8);put(q+60,0,2);
    assert(!dihos_user_elf_validate(q,sizeof(user_cpp_elf),&plan) && plan.relocation_count==cpp_relocations);
    unsigned char *image=calloc(1,(size_t)plan.bytes),*saved=malloc((size_t)plan.bytes);assert(image && saved);
    for(unsigned i=0;i<plan.count;++i)memcpy(image+plan.segments[i].address,user_cpp_elf+plan.segments[i].offset,(size_t)plan.segments[i].file_bytes);
    memcpy(saved,image,(size_t)plan.bytes);
    dihos_user_elf_plan invalid=plan;invalid.relocations[invalid.relocation_count-1].address=plan.bytes;
    assert(dihos_user_elf_relocate(&invalid,image,plan.bytes)<0 && !memcmp(image,saved,(size_t)plan.bytes));
    invalid=plan;invalid.relocations[0].address=0;
    assert(dihos_user_elf_relocate(&invalid,image,plan.bytes)<0 && !memcmp(image,saved,(size_t)plan.bytes));
    assert(!dihos_user_elf_relocate(&plan,image,plan.bytes) && !memcmp(image,saved,4096));
    for(unsigned i=0;i<plan.relocation_count;++i)assert(get(image+plan.relocations[i].address,8)==(uintptr_t)image+plan.relocations[i].addend);
    for(unsigned i=0;i<2000;++i){memcpy(q,user_cpp_elf,sizeof(user_cpp_elf));q[(i*7919u)%sizeof(user_cpp_elf)]^=(uint8_t)(i|1);(void)dihos_user_elf_validate(q,sizeof(user_cpp_elf),&plan);}
    free(saved);free(image);free(q);
    assert(!dihos_user_elf_validate(user_threads_elf,sizeof(user_threads_elf),&plan) && plan.tls_file_bytes==8 && plan.tls_memory_bytes>=24 && plan.tls_alignment==16);
    uint64_t too_many_tls_bytes=plan.tls_memory_bytes+1;
    unsigned char *t=malloc(sizeof(user_threads_elf));assert(t);ph=get(user_threads_elf+32,8);uint64_t tls=0;
    for(unsigned i=0;i<get(user_threads_elf+56,2);++i)if(get(user_threads_elf+ph+i*56,4)==7)tls=ph+i*56;
    assert(tls);
    struct {uint64_t at,value;unsigned bytes;} bad_tls[]={
        {tls+4,5,4},{tls+8,UINT64_MAX,8},{tls+16,UINT64_MAX,8},
        {tls+32,too_many_tls_bytes,8},{tls+40,4081,8},{tls+40,0,8},{tls+48,0,8},{tls+48,32,8},{tls+48,3,8}
    };
    for(unsigned i=0;i<sizeof(bad_tls)/sizeof(bad_tls[0]);++i){memcpy(t,user_threads_elf,sizeof(user_threads_elf));put(t+bad_tls[i].at,bad_tls[i].value,bad_tls[i].bytes);assert(dihos_user_elf_validate(t,sizeof(user_threads_elf),&plan)<0 && !plan.count);}
    memcpy(t,user_threads_elf,sizeof(user_threads_elf));
    uint64_t original_tls=get(t+tls+16,8),thread_rela=0;
    for(unsigned i=0;i<get(t+56,2);++i)if(get(t+ph+i*56,4)==2){
        uint64_t at=get(t+ph+i*56+8,8),size=get(t+ph+i*56+32,8);
        for(uint64_t n=0;n<size;n+=16)if(get(t+at+n,8)==7)thread_rela=offset_for(t,get(t+at+n+8,8));
    }
    assert(thread_rela);put(t+tls+8,get(t+tls+8,8)+1,8);put(t+tls+16,original_tls+1,8);
    put(t+tls+32,7,8);put(t+tls+40,23,8);put(t+tls+48,1,8);put(t+thread_rela,original_tls,8);
    assert(dihos_user_elf_validate(t,sizeof(user_threads_elf),&plan)<0 && !plan.count);
    assert(!dihos_user_elf_validate(user_threads_bss_elf,sizeof(user_threads_bss_elf),&plan) && !plan.tls_file_bytes && plan.tls_memory_bytes>=16);
    free(t);
    puts("PASS: actual ARM64 ELF fixtures, malformed headers/segments, W+X, overlap, bounds, entry validation and 10000 mutations");
    puts("PASS: bounded static PIE RELATIVE records, RO targets, import/TLS/text/duplicate/overflow rejection, stripped section headers, atomic validation and 2000 C++ ELF mutations");
    puts("PASS: bounded initialized and BSS-only local-exec TLS; invalid offsets, sizes, alignment and executable TLS rejected");
}
