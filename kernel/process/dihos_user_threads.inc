/* Native process-local thread service. Only the boot core runs user code;
 * syscall handling runs after the private root has been restored to EL1. */
static uint64_t thread_id(const user_slot *s,unsigned index){return (s->threads[index].generation<<8)|index;}
static uint64_t thread_deadline(uint64_t now,uint64_t delay){return UINT64_MAX-now<delay?UINT64_MAX:now+delay;}
static user_thread *thread_lookup(user_slot *s,uint64_t handle){
    unsigned index=(unsigned)(handle&255);
    if(index>=USER_THREADS || !s->threads[index].state || thread_id(s,index)!=handle)return 0;
    return &s->threads[index];
}
static void thread_unhold(user_thread *t){
    t->held=0;
    if(t->wake_pending && t->state==THREAD_WORD){
        t->state=THREAD_READY;t->wait_word=0;t->cpu.frame.fault.x[0]=0;
    }
    t->wake_pending=0;
}
static void thread_release_inspection(user_slot *s,uint64_t owner){
    if(s->inspect_owner!=owner)return;
    for(unsigned i=0;i<USER_THREADS;++i)thread_unhold(&s->threads[i]);
    s->inspect_owner=0;
}
static int thread_storage_prepare(user_slot *s,unsigned index){
    user_thread *t=&s->threads[index];uint64_t base=(uintptr_t)t->backing,pa;
    int rc=aarch64_user_vm_translate_current(base,&pa);if(rc)return rc;
    for(unsigned i=1;i<USER_THREAD_PAGES;++i){uint64_t next;
        if(aarch64_user_vm_translate_current(base+i*4096,&next) || next!=pa+i*4096)return -14;}
    rc=aarch64_user_vm_prepare_owned(&s->vm,base,pa,USER_THREAD_PAGES*4096);if(rc)return rc;
    uint8_t *stack=(uint8_t*)t->backing+4096,*tls=(uint8_t*)t->backing+USER_TLS_PAGE*4096;
    for(unsigned i=0;i<SACX_USER_THREAD_STACK_BYTES;++i)stack[i]=0;
    for(unsigned i=0;i<4096;++i)tls[i]=0;
    for(unsigned i=0;i<s->info.tls_bytes;++i)tls[16+i]=s->tls_initial[i];
    ((uint64_t*)tls)[1]=thread_id(s,index);
    rc=aarch64_user_vm_set_owned(&s->vm,base+4096,pa+4096,SACX_USER_THREAD_STACK_BYTES,3);
    if(!rc)rc=aarch64_user_vm_set_owned(&s->vm,base+USER_TLS_PAGE*4096,pa+USER_TLS_PAGE*4096,4096,3);
    if(rc)return -1000; /* Fail the process, never free possibly exposed pages. */
    t->physical=pa;t->cpu.root_phys=s->vm.root_phys;
    t->cpu.frame.sp=base+(USER_STACK_PAGES+1)*4096;t->cpu.frame.tpidr=base+USER_TLS_PAGE*4096;
    asm_dma_clean_range(stack,SACX_USER_THREAD_STACK_BYTES);asm_dma_clean_range(tls,4096);
    return aarch64_user_vm_publish(&s->vm)?-1000:0;
}
static int thread_reap(user_slot *s,unsigned index){
    user_thread *t=&s->threads[index];
    if(!index || t->state!=THREAD_DONE || !t->backing)return -22;
    if(aarch64_user_vm_set_owned(&s->vm,(uintptr_t)t->backing,t->physical,USER_THREAD_PAGES*4096,0) ||
       aarch64_user_vm_publish(&s->vm))return -14;
    pmem_free_pages(t->backing,USER_THREAD_PAGES);uint64_t generation=t->generation;
    *t=(user_thread){0};t->generation=generation;return 0;
}
static int64_t thread_service(user_slot *s,unsigned current,uint64_t operation,const uint64_t a[3],uint64_t now){
    user_thread *caller=&s->threads[current];
    switch(operation){
    case SACX_USER_THREAD_CREATE:{
        if(s->info.kind!=6 || aarch64_user_vm_check_execute(&s->vm,a[0]))return -14;
        unsigned index=1;while(index<USER_THREADS && s->threads[index].state)++index;
        if(index==USER_THREADS)return -11;
        user_thread *t=&s->threads[index];uint64_t generation=t->generation+1;
        if(generation>=(1ull<<55))return -75;
        *t=(user_thread){0};t->generation=generation;t->backing=pmem_alloc_pages(USER_THREAD_PAGES);
        if(!t->backing)return -12;
        int rc=thread_storage_prepare(s,index);
        if(rc){
            if(rc==-1000){finish_slot(s,DIHOS_USER_FAILED);return -14;}
            pmem_free_pages(t->backing,USER_THREAD_PAGES);t->backing=0;return rc;
        }
        t->cpu.frame.fault.elr=a[0];t->cpu.frame.fault.x[0]=a[1];t->cpu.frame.fault.x[1]=a[2];
        t->cpu.frame.fpcr=caller->cpu.frame.fpcr;t->cpu.frame.fpsr=caller->cpu.frame.fpsr;
        t->state=THREAD_READY;++s->info.threads_created;return (int64_t)thread_id(s,index);
    }
    case SACX_USER_THREAD_SELF:
        if(a[0] || a[1] || a[2])return -22;
        return (int64_t)thread_id(s,current);
    case SACX_USER_THREAD_EXIT:
        if(!current || a[1] || a[2])return -22;
        thread_release_inspection(s,thread_id(s,current));
        caller->result=a[0];caller->state=THREAD_DONE;++s->info.threads_exited;
        if(caller->detached && thread_reap(s,current)){finish_slot(s,DIHOS_USER_FAILED);return -14;}
        return 0;
    case SACX_USER_THREAD_DETACH:{
        if(a[1] || a[2])return -22;
        user_thread *target=thread_lookup(s,a[0]);if(!target)return -3;
        unsigned index=(unsigned)(target-s->threads);
        if(!index)return -22;
        if(target->joiner || target->held || target->detached)return -16;
        if(target->state==THREAD_DONE){
            if(thread_reap(s,index)){finish_slot(s,DIHOS_USER_FAILED);return -14;}
        }else target->detached=1;
        ++s->info.thread_detaches;
        return 0;
    }
    case SACX_USER_THREAD_PAUSE:{
        _Static_assert(sizeof(sacx_user_thread_context)==848,"thread context wire layout");
        _Static_assert(THREAD_READY==SACX_USER_THREAD_READY && THREAD_SLEEP==SACX_USER_THREAD_SLEEPING &&
            THREAD_JOIN==SACX_USER_THREAD_JOINING && THREAD_WORD==SACX_USER_THREAD_WAITING,"public stopped-thread states");
        if(s->info.kind!=6 || a[2]!=sizeof(sacx_user_thread_context))return -22;
        user_thread *target=thread_lookup(s,a[0]);if(!target || target->state==THREAD_DONE)return -3;
        if(target==caller)return -35;
        uint64_t owner=thread_id(s,current);
        if(target->held || (s->inspect_owner && s->inspect_owner!=owner))return -16;
        if(!target->backing)return -14;
        if(aarch64_user_vm_check_buffer(&s->vm,a[1],sizeof(sacx_user_thread_context),1))return -14;
        // All user execution is serialized on the boot core. This saved frame
        // is stable here, after restoring the private kernel translation root.
        sacx_user_thread_context copy={0};aa64_user_frame *f=&target->cpu.frame;
        copy.version=SACX_USER_CONTEXT_VERSION;copy.bytes=sizeof(copy);copy.state=target->state;copy.thread=a[0];
        for(unsigned i=0;i<31;++i)copy.x[i]=f->fault.x[i];
        for(unsigned i=0;i<64;++i)copy.q[i]=f->q[i];
        copy.sp=f->sp;copy.pc=f->fault.elr;copy.pstate=f->fault.spsr;
        copy.fpcr=f->fpcr;copy.fpsr=f->fpsr;copy.tpidr=f->tpidr;
        copy.stack_begin=(uintptr_t)target->backing+4096;copy.stack_end=copy.stack_begin+SACX_USER_THREAD_STACK_BYTES;
        int rc=aarch64_user_vm_copy_out(&s->vm,a[1],&copy,sizeof(copy));if(rc)return rc;
        target->held=1;s->inspect_owner=owner;return 0;
    }
    case SACX_USER_THREAD_RESUME:{
        if(a[1] || a[2])return -22;
        user_thread *target=thread_lookup(s,a[0]);if(!target || target->state==THREAD_DONE)return -3;
        if(s->inspect_owner!=thread_id(s,current))return -1;
        if(!target->held)return -22;
        thread_unhold(target);unsigned held=0;
        for(unsigned i=0;i<USER_THREADS;++i)held+=s->threads[i].held;
        if(!held)s->inspect_owner=0;return 0;
    }
    case SACX_USER_THREAD_STACK:{
        if(s->info.kind!=6 || a[2]!=sizeof(sacx_user_stack_bounds))return -22;
        user_thread *target=thread_lookup(s,a[0]);if(!target || target->state==THREAD_DONE)return -3;
        if(!target->backing)return -14;
        sacx_user_stack_bounds bounds={(uintptr_t)target->backing+4096,(uintptr_t)target->backing+(USER_STACK_PAGES+1)*4096};
        return aarch64_user_vm_copy_out(&s->vm,a[1],&bounds,sizeof(bounds));
    }
    case SACX_USER_THREAD_JOIN:{
        user_thread *target=thread_lookup(s,a[0]);if(!target)return -3;
        unsigned index=(unsigned)(target-s->threads);
        if(index==current)return -35;
        if(!index || a[2]>60000)return -22;
        if(target->held || target->detached)return -16;
        if(target->joiner)return -16;
        if(a[1] && aarch64_user_vm_check_buffer(&s->vm,a[1],8,1))return -14;
        if(target->state==THREAD_DONE){
            if(a[1] && aarch64_user_vm_copy_out(&s->vm,a[1],&target->result,8))return -14;
            if(thread_reap(s,index)){finish_slot(s,DIHOS_USER_FAILED);return -14;}
            ++s->info.thread_joins;return 0;
        }
        if(!a[2])return -11;
        target->joiner=current+1;caller->join_target=a[0];caller->join_output=a[1];
        caller->wake=thread_deadline(now,a[2]);caller->state=THREAD_JOIN;return 0;
    }
    case SACX_USER_WAIT32:{
        uint32_t word;
        if((a[0]&3) || a[1]>UINT32_MAX || a[2]>60000)return -22;
        if(aarch64_user_vm_copy_in(&s->vm,&word,a[0],4))return -14;
        if(word!=(uint32_t)a[1])return -11;
        if(!a[2]){++s->info.thread_timeouts;return -110;}
        caller->wait_word=a[0];caller->wake=thread_deadline(now,a[2]);caller->state=THREAD_WORD;
        ++s->info.thread_waits;return 0;
    }
    case SACX_USER_WAKE32:{
        uint32_t word;
        if((a[0]&3) || !a[1] || a[1]>USER_THREADS || a[2])return -22;
        if(aarch64_user_vm_copy_in(&s->vm,&word,a[0],4))return -14;
        unsigned woke=0;
        for(unsigned i=0;i<USER_THREADS && woke<a[1];++i){user_thread *t=&s->threads[i];
            if(t->state==THREAD_WORD && t->wait_word==a[0] && !t->wake_pending){
                if(t->held)t->wake_pending=1;
                else {t->state=THREAD_READY;t->wait_word=0;t->cpu.frame.fault.x[0]=0;}
                ++woke;}}
        s->info.thread_wakes+=woke;return woke;
    }
    default:return -38;
    }
}
static int thread_update_waiters(user_slot *s,uint64_t now){
    for(unsigned i=0;i<USER_THREADS;++i){user_thread *t=&s->threads[i];
        if(t->held)continue;
        if(t->state==THREAD_JOIN){
            user_thread *target=thread_lookup(s,t->join_target);
            if(!target){t->cpu.frame.fault.x[0]=(uint64_t)-3;t->state=THREAD_READY;}
            else if(target->state==THREAD_DONE){
                int rc=t->join_output?aarch64_user_vm_copy_out(&s->vm,t->join_output,&target->result,8):0;
                target->joiner=0;
                if(!rc){rc=thread_reap(s,(unsigned)(target-s->threads));if(rc){finish_slot(s,DIHOS_USER_FAILED);return -1;}++s->info.thread_joins;}
                t->cpu.frame.fault.x[0]=(uint64_t)rc;t->state=THREAD_READY;t->join_target=0;
            }else if(now>=t->wake){
                target->joiner=0;t->join_target=0;t->state=THREAD_READY;
                t->cpu.frame.fault.x[0]=(uint64_t)-110;++s->info.thread_timeouts;
            }
        }else if(t->state==THREAD_WORD){
            int error=aarch64_user_vm_check_buffer(&s->vm,t->wait_word,4,0);
            if(error || now>=t->wake){t->state=THREAD_READY;t->wait_word=0;
                t->cpu.frame.fault.x[0]=(uint64_t)(error?-14:-110);if(!error)++s->info.thread_timeouts;}
        }else if(t->state==THREAD_SLEEP && now>=t->wake)t->state=THREAD_READY;
    }
    return 0;
}
