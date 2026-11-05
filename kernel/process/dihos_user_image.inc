/* Incremental static ELF admission. Source ownership moves only on success. */
int dihos_user_spawn_owned_elf_with_network(void *data,uint64_t bytes,uint64_t pages,uint32_t network,dihos_process_handle *out){
    if(network>1)return -22;
    return dihos_user_spawn_owned_elf_with_grants(data,bytes,pages,network?DIHOS_PROCESS_CAP_NETWORK:0,out);
}
static int spawn_owned_with_budget(void *data,uint64_t bytes,uint64_t pages,uint64_t grants,uint64_t memory_limit,dihos_process_handle *out){
    if(grants&~(DIHOS_PROCESS_CAP_NETWORK|DIHOS_PROCESS_CAP_COMPOSITOR_SURFACE))return -22;
    if(!enabled || !out || !data || ((uintptr_t)data&4095) || pages!=(bytes+4095)/4096 || aa64_user_slice_active())return -22;
    dihos_user_elf_plan plan;int rc=dihos_user_elf_headers(data,bytes,&plan);if(rc)return -300+rc;
    unsigned count=0;for(unsigned i=0;i<DIHOS_USER_TASKS;++i)count+=live(&slots[i]);if(count==DIHOS_USER_TASKS)return -2;
    uint64_t fixed=plan.bytes/4096+2+4*USER_THREAD_PAGES+AARCH64_USER_VM_MAX_TABLES;
    if(fixed+pages>memory_limit/4096)return -12;
    dihos_process_desc desc={0};desc.kind=DIHOS_PROCESS_KIND_NATIVE;desc.memory_limit_bytes=memory_limit;
    desc.capabilities=DIHOS_PROCESS_CAP_CLOCK|DIHOS_PROCESS_CAP_LOG|DIHOS_PROCESS_CAP_MEMORY|DIHOS_PROCESS_CAP_THREADS|DIHOS_PROCESS_CAP_RANDOM;
    desc.capabilities|=grants;
    dihos_process_handle handle;rc=dihos_process_create(&processes,&desc,&handle);if(rc)return rc;
    if(handle.slot>=DIHOS_USER_TASKS){dihos_process_fault(&processes,handle);dihos_process_reap(&processes,handle);return -2;}
    user_slot *s=&slots[handle.slot];*s=(user_slot){0};s->info.handle=handle;s->info.kind=6;
    s->image_plan=plan;s->source=data;s->source_pages=pages;s->image_phase=1;
    s->code_pages=plan.bytes/4096+2;s->stack_pages=USER_THREAD_PAGES;s->memory.limit=desc.memory_limit_bytes-fixed*4096;
    s->info.state=DIHOS_USER_LOADING;s->info.load_total=plan.bytes;*out=handle;return 0;
}
int dihos_user_spawn_owned_elf_with_grants(void *data,uint64_t bytes,uint64_t pages,uint64_t grants,dihos_process_handle *out){
    return spawn_owned_with_budget(data,bytes,pages,grants,USER_NATIVE_MEMORY_BYTES,out);
}
/* Kernel-only launch policy; capabilities alone never raise the memory budget. */
int dihos_user_spawn_owned_browser_elf(void *data,uint64_t bytes,uint64_t pages,dihos_process_handle *out){
    return spawn_owned_with_budget(data,bytes,pages,DIHOS_PROCESS_CAP_NETWORK|DIHOS_PROCESS_CAP_COMPOSITOR_SURFACE,768ull*1024*1024,out);
}
int dihos_user_spawn_owned_elf(void *data,uint64_t bytes,uint64_t pages,dihos_process_handle *out){return dihos_user_spawn_owned_elf_with_network(data,bytes,pages,0,out);}
static void image_step(user_slot *s){
    const uint64_t batch=65536;dihos_user_elf_plan *p=&s->image_plan;
    uint8_t *image=s->code?(uint8_t*)s->code+4096:0;int rc=0;
    s->info.admission_step=20+s->image_phase;
    s->info.load_done=s->image_position;
    if(s->image_phase==1){
        unsigned remaining=p->relocation_count-(unsigned)s->image_position,n=remaining>256?256:remaining;
        rc=dihos_user_elf_validate_step(p,(unsigned)s->image_position,n);if(rc)goto failed;
        s->image_position+=n;if(s->image_position<p->relocation_count)return;
        s->image_phase=2;s->image_position=0;return;
    }
    if(s->image_phase==2){
        s->code=pmem_alloc_pages(s->code_pages);s->stack=pmem_alloc_pages(s->stack_pages);
        if(!s->code || !s->stack){rc=-12;goto failed;}
        rc=aarch64_user_vm_create(&s->vm);if(rc)goto failed;
        s->image_phase=3;return;
    }
    if(s->image_phase==3){
        uint64_t n=p->bytes-s->image_position;if(n>batch)n=batch;
        for(uint64_t i=0;i<n;++i)image[s->image_position+i]=0;
        asm_dma_clean_range(image+s->image_position,n);s->image_position+=n;
        if(s->image_position==p->bytes){s->image_phase=4;s->image_position=0;}return;
    }
    if(s->image_phase==4){
        if(s->image_segment==p->count){s->image_phase=5;s->image_position=0;return;}
        dihos_user_elf_segment *segment=&p->segments[s->image_segment];
        uint64_t n=segment->file_bytes-s->image_position;if(n>batch)n=batch;
        for(uint64_t i=0;i<n;++i)image[segment->address+s->image_position+i]=((const uint8_t*)s->source)[segment->offset+s->image_position+i];
        if(n)asm_dma_clean_range(image+segment->address+s->image_position,n);
        s->image_position+=n;if(s->image_position==segment->file_bytes){++s->image_segment;s->image_position=0;}return;
    }
    if(s->image_phase==5){
        unsigned n=p->relocation_count-(unsigned)s->image_position;if(n>256)n=256;
        rc=dihos_user_elf_relocate_step(p,image,p->bytes,(unsigned)s->image_position,n);if(rc)goto failed;
        // Publish only the touched records; do not clean a full large image here.
        for(unsigned i=(unsigned)s->image_position;i<s->image_position+n;++i){
            uint64_t at=p->relocation_count<=DIHOS_USER_ELF_INLINE_RELOCATIONS?p->relocations[i].address:
                0;
            if(p->relocation_count>DIHOS_USER_ELF_INLINE_RELOCATIONS){const uint8_t *r=p->source+p->relocation_offset+(uint64_t)i*24;
                for(unsigned b=0;b<8;++b)at|=(uint64_t)r[b]<<(8*b);}
            asm_dma_clean_range(image+at,8);
        }
        s->image_position+=n;if(s->image_position==p->relocation_count){s->image_phase=6;s->image_position=0;s->image_segment=0;}return;
    }
    if(s->image_phase==6){
        if(s->image_segment==p->count){s->image_phase=7;return;}
        dihos_user_elf_segment *segment=&p->segments[s->image_segment];
        uint64_t total=(segment->memory_bytes+4095)&~4095ull,n=total-s->image_position;if(n>batch)n=batch;
        uint32_t permissions=1|((segment->flags&1)?4:0)|((segment->flags&2)?2:0);
        for(uint64_t i=0;i<n;i+=4096){uint64_t va=(uintptr_t)image+segment->address+s->image_position+i,pa;
            rc=aarch64_user_vm_translate_current(va,&pa);if(rc)goto failed;
            rc=aarch64_user_vm_map(&s->vm,va,pa,4096,permissions);if(rc)goto failed;}
        if(segment->flags&1)asm_sync_executable_range(image+segment->address+s->image_position,n);
        s->image_position+=n;if(s->image_position==total){++s->image_segment;s->image_position=0;}return;
    }
    s->info.tls_bytes=(uint32_t)p->tls_memory_bytes;s->info.relocations=p->relocation_count;
    for(uint64_t i=0;i<p->tls_file_bytes;++i)s->tls_initial[i]=((const uint8_t*)s->source)[p->tls_offset+i];
    s->threads[0].generation=1;s->threads[0].backing=s->stack;
    rc=thread_storage_prepare(s,0);if(rc)goto failed;
    s->threads[0].cpu.frame.fault.elr=(uintptr_t)image+p->entry;
    rc=aarch64_user_vm_publish(&s->vm);if(rc)goto failed;
    rc=dihos_process_set_ready(&processes,s->info.handle);if(rc)goto failed;
    rc=dihos_process_set_running(&processes,s->info.handle);if(rc)goto failed;
    s->info.code_va=(uintptr_t)image;s->info.stack_va=(uintptr_t)s->stack+4096;s->info.backing_alias=1;
    pmem_free_pages(s->source,s->source_pages);s->source=0;p->source=0;
    s->info.load_done=p->bytes;s->threads[0].state=THREAD_READY;s->info.threads_live=1;s->info.state=DIHOS_USER_READY;return;
failed:
    s->info.admission_result=rc;finish_slot(s,DIHOS_USER_FAILED);
}
