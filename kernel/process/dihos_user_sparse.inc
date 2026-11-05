/* Sparse payload backing and metadata are kernel-owned and quota charged. */
typedef struct dihos_user_sparse_chunk {
    struct dihos_user_sparse_chunk *next;
    uint64_t index;
    void *pages[256];
} dihos_user_sparse_chunk;
_Static_assert(sizeof(dihos_user_sparse_chunk)<=4096,"one-page sparse metadata");
static int sparse_overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return n && m && a<b+m && b<a+n;}
static dihos_user_sparse_region *sparse_find(dihos_user_memory *m,uint64_t va,uint64_t bytes){
    for(unsigned i=0;i<DIHOS_USER_SPARSE_REGIONS;++i){dihos_user_sparse_region *r=&m->sparse[i];
        if(r->address && va>=r->address && va-r->address<r->bytes && bytes<=r->bytes-(va-r->address))return r;
    }
    return 0;
}
static dihos_user_sparse_chunk *sparse_chunk(dihos_user_sparse_chunk *list,uint64_t index){
    for(;list;list=list->next)if(list->index==index)return list;return 0;
}
static int sparse_init_arena(dihos_user_memory *m,aarch64_user_vm *vm){
    aa64_user_timer_diagnostic timer;aa64_user_timer_get_diagnostic(&timer);
    if(!timer.config.distributor || !timer.config.redistributor)return -95;
    for(uint64_t base=64ull<<30;base+(16ull<<30)<=(512ull<<30);base+=16ull<<30){
        if(sparse_overlap(base,16ull<<30,timer.config.distributor,65536) ||
           sparse_overlap(base,16ull<<30,timer.config.redistributor,timer.config.redistributor_bytes))continue;
        int rc=aarch64_user_vm_claim_sparse_arena(vm,base,DIHOS_USER_SPARSE_ARENA_BYTES);
        if(!rc){m->sparse_arena=base;return 0;}
        if(rc==-12)return rc;
    }
    return -95;
}
static int64_t sparse_reserve(dihos_user_memory *m,aarch64_user_vm *vm,const uint64_t a[3]){
    uint64_t bytes=a[0],alignment=a[1]?a[1]:4096,guard=a[2]*4096;
    if(!bytes || (bytes&4095) || alignment<4096 || alignment>DIHOS_USER_SPARSE_REGION_MAX ||
       (alignment&(alignment-1)) || a[2]>16)return -22;
    if(bytes>DIHOS_USER_SPARSE_REGION_MAX)return -7;
    dihos_user_sparse_region *slot=0;
    for(unsigned i=0;i<DIHOS_USER_SPARSE_REGIONS;++i)if(!m->sparse[i].address){slot=&m->sparse[i];break;}
    if(!slot)return -12;
    if(!m->sparse_arena){int rc=sparse_init_arena(m,vm);if(rc)return rc;}
    uint64_t address=(m->sparse_arena+guard+alignment-1)&~(alignment-1);
    for(unsigned attempt=0;attempt<=DIHOS_USER_SPARSE_REGIONS;++attempt){
        if(address<m->sparse_arena+guard || address>m->sparse_arena+DIHOS_USER_SPARSE_ARENA_BYTES ||
           bytes+guard>m->sparse_arena+DIHOS_USER_SPARSE_ARENA_BYTES-address)return -12;
        int conflict=0;
        for(unsigned i=0;i<DIHOS_USER_SPARSE_REGIONS;++i){dihos_user_sparse_region *r=&m->sparse[i];
            if(r->address && sparse_overlap(address-guard,bytes+guard*2,r->span_address,r->span_bytes)){
                address=(r->span_address+r->span_bytes+guard+alignment-1)&~(alignment-1);conflict=1;break;
            }
        }
        if(!conflict){
            *slot=(dihos_user_sparse_region){.address=address,.bytes=bytes,.span_address=address-guard,.span_bytes=bytes+guard*2};
            m->reserved+=slot->span_bytes;return (int64_t)address;
        }
    }
    return -12;
}
static void sparse_free_empty(dihos_user_memory *m,dihos_user_sparse_region *r){
    dihos_user_sparse_chunk **at=&r->chunks;
    while(*at){dihos_user_sparse_chunk *c=*at;unsigned i=0;
        while(i<256 && !c->pages[i])++i;
        if(i==256){*at=c->next;pmem_free_pages(c,1);m->charged-=4096;}
        else at=&c->next;
    }
}
static int sparse_apply(dihos_user_memory *m,aarch64_user_vm *vm,dihos_user_sparse_region *r,uint64_t op,uint64_t address,uint64_t bytes,uint32_t permissions){
    unsigned count=(unsigned)(bytes/4096);uint64_t first=(address-r->address)/4096;
    void *backing[AARCH64_USER_VM_SCATTER_MAX]={0};uint64_t physical[AARCH64_USER_VM_SCATTER_MAX];
    dihos_user_sparse_chunk *nodes[AARCH64_USER_VM_SCATTER_MAX],*pending=0;
    unsigned new_nodes=0,allocated=0;int rc=0;
    for(unsigned i=0;i<count;++i){
        nodes[i]=sparse_chunk(r->chunks,(first+i)/256);
        void *page=nodes[i]?nodes[i]->pages[(first+i)%256]:0;
        if(op==SACX_USER_MEMORY_COMMIT && page)return -17;
        if(op!=SACX_USER_MEMORY_COMMIT && !page)return -14;
        backing[i]=page;
    }
    if(op==SACX_USER_MEMORY_COMMIT){
        for(unsigned i=0;i<count;++i)if(!nodes[i]){
            nodes[i]=sparse_chunk(pending,(first+i)/256);
            if(!nodes[i]){
                uint64_t cost=(uint64_t)(count+new_nodes+1)*4096;
                if(m->charged>m->limit || cost>m->limit-m->charged){rc=-12;goto failed;}
                dihos_user_sparse_chunk *c=pmem_alloc_pages(1);if(!c){rc=-12;goto failed;}
                for(unsigned b=0;b<4096;++b)((uint8_t*)c)[b]=0;
                c->index=(first+i)/256;c->next=pending;pending=c;++new_nodes;nodes[i]=c;
            }
        }
        if(m->charged>m->limit || (uint64_t)(count+new_nodes)*4096>m->limit-m->charged){rc=-12;goto failed;}
        for(unsigned i=0;i<count;++i){
            backing[i]=pmem_alloc_pages(1);if(!backing[i]){rc=-12;goto failed;}++allocated;
            if(aarch64_user_vm_translate_current((uintptr_t)backing[i],&physical[i])){rc=-14;goto failed;}
            for(unsigned b=0;b<4096;++b)((uint8_t*)backing[i])[b]=0;
            asm_dma_clean_range(backing[i],4096);
        }
        rc=aarch64_user_vm_map_scatter(vm,address,physical,count,permissions);if(rc)goto failed;
        while(pending){dihos_user_sparse_chunk *next=pending->next;pending->next=r->chunks;r->chunks=pending;pending=next;}
        for(unsigned i=0;i<count;++i)nodes[i]->pages[(first+i)%256]=backing[i];
        m->charged+=(uint64_t)(count+new_nodes)*4096;m->committed+=bytes;
        if(m->committed>m->peak)m->peak=m->committed;return 0;
    }
    for(unsigned i=0;i<count;++i)if(aarch64_user_vm_translate_current((uintptr_t)backing[i],&physical[i]))return -14;
    if(op==SACX_USER_MEMORY_PROTECT)return aarch64_user_vm_protect_scatter(vm,address,physical,count,permissions);
    rc=aarch64_user_vm_unmap_scatter(vm,address,physical,count);if(rc)return rc;
    for(unsigned i=0;i<count;++i){nodes[i]->pages[(first+i)%256]=0;pmem_free_pages(backing[i],1);}
    m->charged-=bytes;m->committed-=bytes;sparse_free_empty(m,r);return 0;
failed:
    for(unsigned i=0;i<allocated;++i)pmem_free_pages(backing[i],1);
    while(pending){dihos_user_sparse_chunk *next=pending->next;pmem_free_pages(pending,1);pending=next;}
    return rc;
}
static int64_t sparse_change(dihos_user_memory *m,aarch64_user_vm *vm,uint64_t op,const uint64_t a[3]){
    dihos_user_sparse_region *r=sparse_find(m,a[0],a[1]);if(!r)return -14;
    if(r->releasing)return -16;
    return sparse_apply(m,vm,r,op,a[0],a[1],(uint32_t)a[2]);
}
static int64_t sparse_release(dihos_user_memory *m,aarch64_user_vm *vm,const uint64_t a[3]){
    if(a[1] || a[2])return -22;
    dihos_user_sparse_region *r=sparse_find(m,a[0],1);if(!r || r->address!=a[0])return -22;
    r->releasing=1;
    if(r->chunks){
        dihos_user_sparse_chunk *c=r->chunks;unsigned first=0;
        while(first<256 && !c->pages[first])++first;
        if(first==256)return -14;
        unsigned n=1;while(n<16 && first+n<256 && c->pages[first+n])++n;
        int rc=sparse_apply(m,vm,r,SACX_USER_MEMORY_DECOMMIT,r->address+(c->index*256+first)*4096,(uint64_t)n*4096,0);
        if(rc)return rc;
    }
    if(r->chunks)return -11;
    m->reserved-=r->span_bytes;*r=(dihos_user_sparse_region){0};return 0;
}
static void sparse_destroy(dihos_user_memory *m){
    for(unsigned i=0;i<DIHOS_USER_SPARSE_REGIONS;++i){dihos_user_sparse_chunk *c=m->sparse[i].chunks;
        while(c){dihos_user_sparse_chunk *next=c->next;
            for(unsigned p=0;p<256;++p)if(c->pages[p])pmem_free_pages(c->pages[p],1);
            pmem_free_pages(c,1);c=next;
        }
    }
}
