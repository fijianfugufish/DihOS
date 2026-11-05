#include "process/dihos_user_memory.h"
#include "../../sdk/sacx/include/sacx_user_syscall.h"
#if defined(__aarch64__)
#include "memory/pmem.h"
#include "asm/asm.h"
#include "asm/aa64_user_slice.h"
#include "dihos_user_sparse.inc"
static dihos_user_region *find(dihos_user_memory *m,uint64_t va,uint64_t bytes){
    for(unsigned i=0;i<DIHOS_USER_MEMORY_REGIONS;++i){
        dihos_user_region *r=&m->regions[i];
        if(r->backing && va>=r->address && va-r->address<r->bytes &&
           bytes<=r->bytes-(va-r->address))return r;
    }
    return 0;
}
int64_t dihos_user_memory_call(dihos_user_memory *m,aarch64_user_vm *vm,uint64_t op,const uint64_t a[3]){
    if(!m || !vm || !vm->root_phys)return -22;
    if(op==SACX_USER_SPARSE_RESERVE)return sparse_reserve(m,vm,a);
    if(op==SACX_USER_SPARSE_RELEASE)return sparse_release(m,vm,a);
    if(op==SACX_USER_MEMORY_RESERVE){
        uint64_t bytes=a[0];
        if(!bytes || (bytes&4095) || a[1] || a[2])return -22;
        if(bytes>DIHOS_USER_MEMORY_REGION_MAX)return -7;
        uint64_t charged=bytes+8192;
        if(m->charged>m->limit || charged>m->limit-m->charged)return -12;
        dihos_user_region *r=0;
        for(unsigned i=0;i<DIHOS_USER_MEMORY_REGIONS;++i)if(!m->regions[i].backing){r=&m->regions[i];break;}
        if(!r)return -12;
        void *backing=pmem_alloc_pages(charged/4096);if(!backing)return -12;
        uint64_t va=(uintptr_t)backing+4096,pa;
        int rc=aarch64_user_vm_translate_current(va,&pa);
        if(rc){pmem_free_pages(backing,charged/4096);return rc;}
        /* Uncommitted pages retain inherited EL1-only mappings. Page-table
         * preparation and zeroing are limited to each <=64 KiB commit batch.
         * All prior owners revoke their user leaves before freeing backing. */
        *r=(dihos_user_region){0};r->backing=backing;r->address=va;r->physical=pa;r->bytes=bytes;
        m->charged+=charged;m->reserved+=bytes;
        aarch64_user_vm_publish(vm);return (int64_t)va;
    }
    if(op==SACX_USER_MEMORY_RELEASE){
        if(a[1] || a[2])return -22;
        dihos_user_region *r=find(m,a[0],1);
        if(!r || r->address!=a[0])return -22;
        for(unsigned i=0;i<r->bytes/4096;++i)if(r->committed[i/64]&(1ull<<(i%64))){
            int rc=aarch64_user_vm_set_owned(vm,r->address+(uint64_t)i*4096,r->physical+(uint64_t)i*4096,4096,0);if(rc)return rc;
        }
        if(aarch64_user_vm_publish(vm))return -14;
        for(unsigned i=0;i<r->bytes/4096;++i)if(r->committed[i/64]&(1ull<<(i%64)))m->committed-=4096;
        m->charged-=r->bytes+8192;m->reserved-=r->bytes;
        pmem_free_pages(r->backing,r->bytes/4096+2);*r=(dihos_user_region){0};return 0;
    }
    if(op!=SACX_USER_MEMORY_COMMIT && op!=SACX_USER_MEMORY_PROTECT && op!=SACX_USER_MEMORY_DECOMMIT)return -38;
    if(!a[1] || ((a[0]|a[1])&4095) ||
       (a[2]!=0 && a[2]!=SACX_USER_MEMORY_READ && a[2]!=(SACX_USER_MEMORY_READ|SACX_USER_MEMORY_WRITE)))return -22;
    if(a[1]>DIHOS_USER_MEMORY_STEP_MAX)return -7;
    if(op==SACX_USER_MEMORY_COMMIT && !a[2])return -22;
    if(op==SACX_USER_MEMORY_DECOMMIT && a[2])return -22;
    dihos_user_region *r=find(m,a[0],a[1]);
    if(!r)return sparse_change(m,vm,op,a);
    uint64_t first=(a[0]-r->address)/4096,pages=a[1]/4096;
    for(uint64_t i=first;i<first+pages;++i){int set=(r->committed[i/64]>>(i%64))&1;
        if(op==SACX_USER_MEMORY_COMMIT && set)return -17;
        if((op==SACX_USER_MEMORY_PROTECT || op==SACX_USER_MEMORY_DECOMMIT) && !set)return -14;
    }
    int prepared=aarch64_user_vm_prepare_owned(vm,a[0],r->physical+first*4096,a[1]);if(prepared)return prepared;
    if(op==SACX_USER_MEMORY_COMMIT){
        uint8_t *payload=(uint8_t*)r->backing+4096+first*4096;
        for(uint64_t i=0;i<a[1];++i)payload[i]=0;
        asm_dma_clean_range(payload,a[1]);
    }
    int rc=aarch64_user_vm_set_owned(vm,a[0],r->physical+first*4096,a[1],(uint32_t)a[2]);if(rc)return rc;
    if(aarch64_user_vm_publish(vm))return -14;
    if(op==SACX_USER_MEMORY_COMMIT){for(uint64_t i=first;i<first+pages;++i)r->committed[i/64]|=1ull<<(i%64);
        m->committed+=a[1];if(m->committed>m->peak)m->peak=m->committed;}
    if(op==SACX_USER_MEMORY_DECOMMIT){
        for(uint64_t i=first;i<first+pages;++i)r->committed[i/64]&=~(1ull<<(i%64));
        m->committed-=a[1];
    }
    return 0;
}
void dihos_user_memory_destroy(dihos_user_memory *m){
    if(!m)return;
    sparse_destroy(m);
    for(unsigned i=0;i<DIHOS_USER_MEMORY_REGIONS;++i)
        if(m->regions[i].backing)pmem_free_pages(m->regions[i].backing,m->regions[i].bytes/4096+2);
    *m=(dihos_user_memory){0};
}
#else
int64_t dihos_user_memory_call(dihos_user_memory *m,aarch64_user_vm *vm,uint64_t op,const uint64_t a[3]){(void)m;(void)vm;(void)op;(void)a;return -38;}
void dihos_user_memory_destroy(dihos_user_memory *m){if(m)*m=(dihos_user_memory){0};}
#endif
