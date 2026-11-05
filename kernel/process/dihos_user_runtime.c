#include "process/dihos_user_runtime.h"
#include "process/dihos_user_elf.h"
#include "process/dihos_user_sacx.h"
#include "process/dihos_user_memory.h"
#include "process/dihos_user_network.h"
#include "process/dihos_user_window.h"
#include "process/dihos_browser_bridge.h"
#pragma weak dihos_browser_native_call
#pragma weak dihos_user_network_call
#pragma weak dihos_user_network_cleanup
#pragma weak dihos_user_window_call
#pragma weak dihos_user_window_cleanup
_Static_assert(DIHOS_PROCESS_CAP_CLOCK==SACX_USER_CAP_CLOCK,"shared clock capability bit");
_Static_assert(DIHOS_PROCESS_CAP_LOG==SACX_USER_CAP_LOG,"shared log capability bit");
_Static_assert(DIHOS_PROCESS_CAP_MEMORY==SACX_USER_CAP_MEMORY,"shared memory capability bit");
_Static_assert(DIHOS_PROCESS_CAP_THREADS==SACX_USER_CAP_THREADS,"shared threads capability bit");
_Static_assert(DIHOS_PROCESS_CAP_RANDOM==SACX_USER_CAP_RANDOM,"shared random capability bit");
#if defined(__aarch64__)
#include "../../sdk/sacx/include/sacx_user_thread_context.h"
#include "../../sdk/sacx/include/sacx_user_resources.h"
#include "asm/aa64_user_slice.h"
#include "asm/asm.h"
#include "memory/pmem.h"
#include "system/cpu_info.h"
#include "system/dihos_time.h"

#define USER_THREADS 4u
#define USER_STACK_PAGES (SACX_USER_THREAD_STACK_BYTES/4096u)
#define USER_THREAD_PAGES (USER_STACK_PAGES+5u)
#define USER_TLS_PAGE (USER_STACK_PAGES+3u)
// Admission accounts for both immutable source and inactive image before
// source release. The native Page's measured peak is approximately 221 MiB.
#define USER_NATIVE_MEMORY_BYTES (256ull*1024*1024)
enum { THREAD_FREE,THREAD_READY,THREAD_SLEEP,THREAD_JOIN,THREAD_WORD,THREAD_DONE };
typedef struct user_thread {
    aa64_user_cpu cpu;
    void *backing;
    uint64_t physical,generation,wake,result,join_target,join_output,wait_word;
    uint64_t user_ticks,kernel_ticks;
    uint32_t state,joiner,held,wake_pending,detached;
} user_thread;
typedef struct user_slot {
    dihos_user_task_info info;
    aarch64_user_vm vm;
    user_thread threads[USER_THREADS];
    void *code,*stack;
    uint64_t code_pages,stack_pages;
    uint32_t thread_cursor;
    uint64_t inspect_owner;
    uint64_t user_ticks,kernel_ticks;
    uint8_t tls_initial[4080];
    dihos_user_memory memory;
    dihos_user_elf_plan image_plan;
    void *source;
    uint64_t source_pages,image_position;
    uint32_t image_phase,image_segment;
} user_slot;
static dihos_process_table processes;
static user_slot slots[DIHOS_USER_TASKS];
static uint32_t enabled,cursor,initialized;
static dihos_process_handle interactive_handle;
static uint32_t interactive_us;
#define USER_TIMER_PROBE 5u

static int live(const user_slot *s){return s->info.state==DIHOS_USER_READY || s->info.state==DIHOS_USER_WAITING || s->info.state==DIHOS_USER_LOADING;}
static void release_memory(user_slot *s){
    if(dihos_user_window_cleanup)dihos_user_window_cleanup(((uint64_t)s->info.handle.generation<<8)|(s->info.handle.slot+1u));
    if(dihos_user_network_cleanup)dihos_user_network_cleanup(((uint64_t)s->info.handle.generation<<8)|(s->info.handle.slot+1u));
    aarch64_user_vm_release(&s->vm);
    for(unsigned i=1;i<USER_THREADS;++i)if(s->threads[i].backing)pmem_free_pages(s->threads[i].backing,USER_THREAD_PAGES);
    for(unsigned i=0;i<USER_THREADS;++i)s->threads[i]=(user_thread){0};
    s->info.threads_live=0;
    s->info.threads_held=0;s->info.inspection_owner=0;
    dihos_user_memory_destroy(&s->memory);
    s->info.memory_reserved=s->info.memory_committed=0;
    if(s->code)pmem_free_pages(s->code,s->code_pages);
    if(s->stack)pmem_free_pages(s->stack,s->stack_pages);
    if(s->source)pmem_free_pages(s->source,s->source_pages);
    s->source=0;
    s->code=s->stack=0;
}
static void finish_slot(user_slot *s,uint32_t state){
    if(state==DIHOS_USER_FAILED || s->info.state==DIHOS_USER_LOADING)dihos_process_fault(&processes,s->info.handle);
    else if(dihos_process_exit(&processes,s->info.handle))dihos_process_fault(&processes,s->info.handle);
    release_memory(s);dihos_process_reap(&processes,s->info.handle);s->info.state=state;
}
#include "dihos_user_threads.inc"
int dihos_user_runtime_enable(void){
    if(enabled)return 0;
    cpu_info_snapshot info;cpu_info_get(&info);
    if(!info.initialized)return -100;
    if(info.gic_version!=3 && info.gic_version!=4)return -101;
    if(info.virtual_timer_irq<16 || info.virtual_timer_irq>31)return -102;
    aa64_user_timer_config config={info.gic_distributor,info.gic_redistributor,info.gic_redistributor_bytes,info.virtual_timer_irq,info.virtual_timer_flags};
    int rc=aa64_user_timer_init(&config);if(rc)return rc;
    if(!initialized){dihos_process_init(&processes);initialized=1;}
    enabled=1;
    /* Prove timer delivery with a finite program before admitting an
     * unbounded loop. A missing IRQ returns an error after the counter ends,
     * rather than hanging the desktop during first use on a new platform. */
    dihos_process_handle probe;
    if(dihos_user_spawn_fixture(USER_TIMER_PROBE,&probe)){enabled=0;return -200;}
    user_slot *s=&slots[probe.slot];
    int event=aa64_user_slice_run(&s->threads[0].cpu,1000);
    finish_slot(s,DIHOS_USER_STOPPED);s->info=(dihos_user_task_info){0};
    if(event!=AA64_USER_PREEMPTED){enabled=0;return -6;}
    return 0;
}
int dihos_user_spawn_fixture(uint32_t kind,dihos_process_handle *out){
    if(!enabled || !out || kind<DIHOS_USER_LOOP || kind>USER_TIMER_PROBE)return -1;
    unsigned count=0;for(unsigned i=0;i<DIHOS_USER_TASKS;++i)count+=(unsigned)live(&slots[i]);
    if(count==DIHOS_USER_TASKS)return -2;
    dihos_process_desc desc={0};desc.kind=DIHOS_PROCESS_KIND_NATIVE;desc.memory_limit_bytes=128*1024;desc.capabilities=DIHOS_PROCESS_CAP_CLOCK;
    dihos_process_handle handle;int rc=dihos_process_create(&processes,&desc,&handle);if(rc)return rc;
    if(handle.slot>=DIHOS_USER_TASKS){dihos_process_fault(&processes,handle);dihos_process_reap(&processes,handle);return -2;}
    user_slot *s=&slots[handle.slot];*s=(user_slot){0};s->info.handle=handle;s->info.kind=kind;
    s->code_pages=s->stack_pages=3;
    /* Own a middle payload page and both neighbors. In a fully mapped boot
     * address space these neighbors remain EL1-only stack/code guards. */
    s->info.admission_step=1;s->code=pmem_alloc_pages(3);
    if(!s->code){s->info.admission_result=-1;goto fail;}
    s->info.admission_step=2;s->stack=pmem_alloc_pages(3);
    if(!s->stack){s->info.admission_result=-1;goto fail;}
    s->info.admission_step=3;
    s->info.admission_result=aarch64_user_vm_create(&s->vm);
    if(s->info.admission_result)goto fail;
    uint64_t code_pa,stack_pa;
    uint8_t *code=(uint8_t*)s->code+4096,*stack=(uint8_t*)s->stack+4096;
    s->info.admission_step=4;s->info.admission_result=aarch64_user_vm_translate_current((uintptr_t)code,&code_pa);
    if(s->info.admission_result)goto fail;
    s->info.admission_step=5;s->info.admission_result=aarch64_user_vm_translate_current((uintptr_t)stack,&stack_pa);
    if(s->info.admission_result)goto fail;
    /* These are trusted kernel-built fixtures. There is no arbitrary image
     * admission yet. RX code and RW stack have separate backing pages. */
    static const uint32_t programs[5][8]={
        {0x91000400,0x17ffffff},
        {0xd28009a0,0xd2800008,0xd4000001,0x14000000},
        {0xd2800001,0xf9400020,0x14000000},
        {0xd2800140,0xd2800048,0xd4000001,0xd2800b00,0xd2800008,0xd4000001,0x14000000},
        {0xd292d001,0xf2a01301,0xf1000421,0x54ffffe1,0xd2800000,0xd2800008,0xd4000001,0x14000000}
    };
    for(unsigned i=0;i<4096;++i)code[i]=stack[i]=0;
    for(unsigned i=0;i<8;++i)((uint32_t*)code)[i]=programs[kind-1][i];
    asm_sync_executable_range(code,32);
    /* Prefer spare aliases, but firmware can map the whole TTBR0 space.
     * The VM mapper also permits replacing precisely our own backing pages
     * in the private clone. It verifies the existing physical translation;
     * unrelated kernel mappings are never replaced. */
    uint64_t code_va=0,stack_va=0,ignored;
    for(unsigned candidate=0;candidate<64;++candidate){
        uint64_t base=(1ull<<(s->vm.va_bits-1))+(uint64_t)candidate*0x200000;
        if(aarch64_user_vm_translate(&s->vm,base,&ignored) &&
           aarch64_user_vm_translate(&s->vm,base+0x10000,&ignored)){
            code_va=base;stack_va=base+0x10000;break;
        }
    }
    s->info.admission_step=6;
    if(!code_va){
        code_va=(uintptr_t)code;stack_va=(uintptr_t)stack;
        s->info.backing_alias=1;
    }
    s->info.code_va=code_va;s->info.stack_va=stack_va;
    s->info.admission_step=7;s->info.admission_result=aarch64_user_vm_map(&s->vm,code_va,code_pa,4096,AARCH64_USER_VM_READ|AARCH64_USER_VM_EXECUTE);
    if(s->info.admission_result)goto fail;
    s->info.admission_step=8;s->info.admission_result=aarch64_user_vm_map(&s->vm,stack_va,stack_pa,4096,AARCH64_USER_VM_READ|AARCH64_USER_VM_WRITE);
    if(s->info.admission_result)goto fail;
    s->threads[0].cpu.root_phys=s->vm.root_phys;s->threads[0].cpu.frame.fault.elr=code_va;s->threads[0].cpu.frame.sp=stack_va+4096;
    s->info.admission_step=9;s->info.admission_result=dihos_process_set_ready(&processes,handle);
    if(s->info.admission_result)goto fail;
    s->info.admission_step=10;s->info.admission_result=dihos_process_set_running(&processes,handle);
    if(s->info.admission_result)goto fail;
    s->info.admission_step=12;s->info.admission_result=aarch64_user_vm_publish(&s->vm);
    if(s->info.admission_result)goto fail;
    s->info.admission_step=0;
    s->threads[0].state=THREAD_READY;s->threads[0].generation=1;s->info.threads_live=1;
    s->info.state=DIHOS_USER_READY;*out=handle;return 0;
fail:
    release_memory(s);dihos_process_fault(&processes,handle);dihos_process_reap(&processes,handle);s->info.state=DIHOS_USER_FAILED;return -3;
}
int dihos_user_spawn_elf(const void *data,uint64_t bytes,dihos_process_handle *out){
    if(bytes>256*1024)return -7;
    if(!enabled || !out || aa64_user_slice_active())return -1;
    dihos_user_elf_plan plan;int rc=dihos_user_elf_validate(data,bytes,&plan);if(rc)return -300+rc;
    unsigned count=0;for(unsigned i=0;i<DIHOS_USER_TASKS;++i)count+=(unsigned)live(&slots[i]);
    if(count==DIHOS_USER_TASKS)return -2;
    if(plan.bytes>256*1024)return -7;
    dihos_process_desc desc={0};desc.kind=DIHOS_PROCESS_KIND_NATIVE;desc.memory_limit_bytes=USER_NATIVE_MEMORY_BYTES;desc.capabilities=DIHOS_PROCESS_CAP_CLOCK|DIHOS_PROCESS_CAP_LOG|DIHOS_PROCESS_CAP_MEMORY|DIHOS_PROCESS_CAP_THREADS|DIHOS_PROCESS_CAP_RANDOM;
    dihos_process_handle handle;rc=dihos_process_create(&processes,&desc,&handle);if(rc)return rc;
    if(handle.slot>=DIHOS_USER_TASKS){dihos_process_fault(&processes,handle);dihos_process_reap(&processes,handle);return -2;}
    user_slot *s=&slots[handle.slot];*s=(user_slot){0};s->info.handle=handle;s->info.kind=6;
    s->code_pages=plan.bytes/4096+2;s->stack_pages=USER_THREAD_PAGES;
    /* Charge image/stack guards and the maximum private-table allowance before
     * exposing a heap budget; reservations also charge both heap guards. */
    s->memory.limit=desc.memory_limit_bytes-(s->code_pages+s->stack_pages+3*USER_THREAD_PAGES+AARCH64_USER_VM_MAX_TABLES)*4096;
    s->code=pmem_alloc_pages(s->code_pages);s->stack=pmem_alloc_pages(s->stack_pages);
    if(!s->code || !s->stack){rc=-1;goto fail_image;}
    rc=aarch64_user_vm_create(&s->vm);if(rc)goto fail_image;
    uint8_t *image=(uint8_t*)s->code+4096,*stack=(uint8_t*)s->stack+4096;
    for(uint64_t i=0;i<plan.bytes;++i)image[i]=0;
    for(unsigned n=0;n<plan.count;++n){
        const dihos_user_elf_segment *seg=&plan.segments[n];
        for(uint64_t i=0;i<seg->file_bytes;++i)image[seg->address+i]=((const uint8_t*)data)[seg->offset+i];
        uint32_t perms=AARCH64_USER_VM_READ|((seg->flags&1)?AARCH64_USER_VM_EXECUTE:0)|((seg->flags&2)?AARCH64_USER_VM_WRITE:0);
        uint64_t mapped=(seg->memory_bytes+4095)&~4095ull;
        for(uint64_t offset=0;offset<mapped;offset+=4096){
            uint64_t va=(uintptr_t)image+seg->address+offset,pa;
            rc=aarch64_user_vm_translate_current(va,&pa);if(rc)goto fail_image;
            rc=aarch64_user_vm_map(&s->vm,va,pa,4096,perms);if(rc)goto fail_image;
        }
        if(seg->flags&1)asm_sync_executable_range(image+seg->address,mapped);
    }
    rc=dihos_user_elf_relocate(&plan,image,plan.bytes);if(rc)goto fail_image;
    s->info.relocations=plan.relocation_count;
    s->info.tls_bytes=(uint32_t)plan.tls_memory_bytes;
    for(uint64_t i=0;i<plan.tls_file_bytes;++i)s->tls_initial[i]=((const uint8_t*)data)[plan.tls_offset+i];
    s->threads[0].generation=1;s->threads[0].backing=s->stack;
    rc=thread_storage_prepare(s,0);if(rc)goto fail_image;
    s->threads[0].cpu.root_phys=s->vm.root_phys;s->threads[0].cpu.frame.fault.elr=(uintptr_t)image+plan.entry;s->threads[0].cpu.frame.sp=(uintptr_t)stack+SACX_USER_THREAD_STACK_BYTES;
    s->info.code_va=(uintptr_t)image;s->info.stack_va=(uintptr_t)stack;s->info.backing_alias=1;
    rc=dihos_process_set_ready(&processes,handle);if(rc)goto fail_image;
    rc=dihos_process_set_running(&processes,handle);if(rc)goto fail_image;
    /* User mappings can differ from the firmware's memory attributes.
     * Publish initialized payloads and descriptor pages before admission. */
    asm_dma_clean_range(image,plan.bytes);
    rc=aarch64_user_vm_publish(&s->vm);if(rc)goto fail_image;
    s->threads[0].state=THREAD_READY;s->threads[0].generation=1;s->info.threads_live=1;
    s->info.state=DIHOS_USER_READY;*out=handle;return 0;
fail_image:
    s->info.admission_step=11;s->info.admission_result=rc;
    release_memory(s);dihos_process_fault(&processes,handle);dihos_process_reap(&processes,handle);s->info.state=DIHOS_USER_FAILED;return -400+rc;
}
#include "dihos_user_image.inc"
static uint64_t resource_add(uint64_t a,uint64_t b){return UINT64_MAX-a<b?UINT64_MAX:a+b;}
static uint64_t resource_clock(void){uint64_t value;__asm__ volatile("isb; mrs %0,cntvct_el0":"=r"(value)::"memory");return value;}
static uint64_t resource_ns(uint64_t ticks,uint64_t frequency){
    uint64_t seconds=ticks/frequency;
    if(seconds>UINT64_MAX/1000000000ull)return UINT64_MAX;
    /* CNTFRQ is a 32-bit architectural value, so the remainder product fits. */
    return resource_add(seconds*1000000000ull,(ticks%frequency)*1000000000ull/frequency);
}
static int64_t resource_snapshot(user_slot *s,user_thread *t,uint64_t output){
    uint64_t frequency;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(frequency));
    if(!frequency || frequency>UINT32_MAX)return -61;
    sacx_user_resources r={.version=SACX_USER_RESOURCES_VERSION,.bytes=sizeof(r),
        .flags=SACX_USER_RESOURCES_CPU|SACX_USER_RESOURCES_MEMORY,
        .thread_user_ns=resource_ns(t->user_ticks,frequency),.thread_kernel_ns=resource_ns(t->kernel_ticks,frequency),
        .process_user_ns=resource_ns(s->user_ticks,frequency),.process_kernel_ns=resource_ns(s->kernel_ticks,frequency),
        .heap_reserved_bytes=s->memory.reserved,.heap_committed_bytes=s->memory.committed,.heap_limit_bytes=s->memory.limit};
    uint64_t pages=s->code_pages+s->stack_pages+s->vm.owned_table_count;
    for(unsigned i=1;i<USER_THREADS;++i)if(s->threads[i].backing)pages+=USER_THREAD_PAGES;
    if(s->source)pages+=s->source_pages;
    r.private_bytes=pages*4096+s->memory.charged;
    return aarch64_user_vm_copy_out(&s->vm,output,&r,sizeof(r));
}
/* 0: idle, 1: short syscall (may batch), 2: return to the desktop. */
static int runtime_step(uint32_t quantum_us){
    if(!enabled)return 0;
    uint64_t now=dihos_time_monotonic_ms();
    for(unsigned pass=0;pass<DIHOS_USER_TASKS;++pass){
        unsigned index=(cursor+pass)%DIHOS_USER_TASKS;user_slot *s=&slots[index];
        if(!live(s))continue;
        // Image work is already bounded (64 KiB or 256 relocation records).
        // Batch it within service's existing 1 ms / 64-step deadline rather
        // than stretching a large image over thousands of desktop frames.
        if(s->info.state==DIHOS_USER_LOADING){image_step(s);cursor=(index+1)%DIHOS_USER_TASKS;return 1;}
        if(thread_update_waiters(s,now))continue;
        unsigned selected=USER_THREADS;
        for(unsigned n=0;n<USER_THREADS;++n){unsigned i=(s->thread_cursor+n)%USER_THREADS;
            if(s->threads[i].state==THREAD_READY && !s->threads[i].held){selected=i;break;}}
        if(selected==USER_THREADS){s->info.state=DIHOS_USER_WAITING;continue;}
        s->info.state=DIHOS_USER_READY;s->thread_cursor=(selected+1)%USER_THREADS;cursor=(index+1)%DIHOS_USER_TASKS;
        user_thread *thread=&s->threads[selected];aa64_user_cpu *cpu=&thread->cpu;
        uint64_t active_start=resource_clock();
        uint32_t allowed_us=s->info.handle.slot==interactive_handle.slot && s->info.handle.generation==interactive_handle.generation?interactive_us:1000;
        if(!allowed_us)allowed_us=1000;
        if(quantum_us>allowed_us)quantum_us=allowed_us;
        int event=aa64_user_slice_run(cpu,quantum_us),batchable=0;
        uint64_t active_end=resource_clock(),active_ticks=active_end-active_start;
        thread->user_ticks=resource_add(thread->user_ticks,active_ticks);
        s->user_ticks=resource_add(s->user_ticks,active_ticks);
        ++s->info.slices;s->info.counter=cpu->frame.fault.x[0];s->info.pc=cpu->frame.fault.elr;
        s->info.esr=cpu->frame.fault.esr;s->info.far=cpu->frame.fault.far;
        if(event==AA64_USER_SACX_REQUEST){
            uint64_t service_start=resource_clock(),generation=thread->generation;
            dihos_process_info process;uint64_t grants=0;
            if(!dihos_process_query(&processes,s->info.handle,&process))grants=process.capabilities;
            uint64_t operation=cpu->frame.fault.x[0];
            uint64_t arguments[3]={cpu->frame.fault.x[1],cpu->frame.fault.x[2],cpu->frame.fault.x[3]};
            dihos_user_sacx_reply reply;
            dihos_user_sacx_service(&s->vm,grants,cpu->frame.fault.x[7],operation,arguments,dihos_time_monotonic_ms(),&reply);
            if(reply.action==DIHOS_SACX_MEMORY){
                reply.result=dihos_user_memory_call(&s->memory,&s->vm,operation,arguments);
                s->info.memory_reserved=s->memory.reserved;s->info.memory_committed=s->memory.committed;s->info.memory_peak=s->memory.peak;
            }else if(reply.action==DIHOS_SACX_THREAD)reply.result=thread_service(s,selected,operation,arguments,dihos_time_monotonic_ms());
            else if(reply.action==DIHOS_SACX_IDENTITY)
                reply.result=((uint64_t)s->info.handle.generation<<8)|(s->info.handle.slot+1u);
            else if(reply.action==DIHOS_SACX_RESOURCES)reply.result=resource_snapshot(s,thread,arguments[0]);
            else if(reply.action==DIHOS_SACX_NETWORK)reply.result=dihos_user_network_call?
                dihos_user_network_call(&s->vm,((uint64_t)s->info.handle.generation<<8)|(s->info.handle.slot+1u),operation,arguments):-38;
            else if(reply.action==DIHOS_SACX_WINDOW)reply.result=dihos_user_window_call?
                dihos_user_window_call(&s->vm,((uint64_t)s->info.handle.generation<<8)|(s->info.handle.slot+1u),operation,arguments):-38;
            else if(reply.action==DIHOS_SACX_BROWSER)reply.result=dihos_browser_native_call?
                dihos_browser_native_call(&s->vm,((uint64_t)s->info.handle.generation<<8)|(s->info.handle.slot+1u),
                    (uint32_t)arguments[0],arguments[1],(uint32_t)arguments[2]):-38;
            uint64_t service_ticks=resource_clock()-service_start;
            s->kernel_ticks=resource_add(s->kernel_ticks,service_ticks);
            if(thread->state!=THREAD_FREE && thread->generation==generation)
                thread->kernel_ticks=resource_add(thread->kernel_ticks,service_ticks);
            ++s->info.sacx_calls;s->info.granted_capabilities=grants;s->info.sacx_last_result=reply.result;
            if(!live(s))return 2;
            cpu->frame.fault.x[0]=(uint64_t)reply.result;
            if(reply.log_bytes)for(unsigned i=0;i<=reply.log_bytes;++i)s->info.last_log[i]=reply.log[i];
            if(reply.action==DIHOS_SACX_SLEEP){thread->wake=thread_deadline(dihos_time_monotonic_ms(),reply.argument);thread->state=THREAD_SLEEP;}
            else if(reply.action==DIHOS_SACX_EXIT){s->info.exit_status=reply.argument;finish_slot(s,DIHOS_USER_DONE);}
            batchable=live(s) && operation!=SACX_USER_YIELD && !reply.log_bytes;
        }else if(event==AA64_USER_PREEMPTED)++s->info.preemptions;
        else if(event==AA64_USER_SLEEPING){thread->wake=thread_deadline(dihos_time_monotonic_ms(),cpu->sleep_ms);thread->state=THREAD_SLEEP;}
        else if(event==AA64_USER_EXITED){s->info.exit_status=cpu->exit_status;finish_slot(s,DIHOS_USER_DONE);}
        else if(event!=AA64_USER_YIELDED){
            __asm__ volatile("mrs %0,tcr_el1":"=r"(s->info.tcr));
            s->info.fault_level=aarch64_user_vm_describe(&s->vm,s->info.far,s->info.fault_walk);
            s->info.hardware_root=cpu->fault_root;s->info.expected_root=cpu->root_phys;
            s->info.hardware_par=cpu->fault_par;s->info.sctlr=cpu->fault_sctlr;
            s->info.fault_thread=thread_id(s,selected);finish_slot(s,DIHOS_USER_FAILED);
            if(event==AA64_USER_IRQ_ERROR || event<0){enabled=0;dihos_user_runtime_stop_all();}
        }
        if(live(s)){s->info.threads_live=0;s->info.threads_held=0;s->info.inspection_owner=s->inspect_owner;
            for(unsigned i=0;i<USER_THREADS;++i){
                s->info.threads_held+=s->threads[i].held;
                if(s->threads[i].state!=THREAD_FREE && s->threads[i].state!=THREAD_DONE)++s->info.threads_live;
            }
        }
        return batchable?1:2;
    }
    return 0;
}
void dihos_user_runtime_poll(void){(void)runtime_step(1000);}
int dihos_user_runtime_set_interactive(dihos_process_handle handle,uint32_t quantum_us){
    if(handle.slot>=DIHOS_USER_TASKS || !handle.generation || quantum_us<1000 || quantum_us>4000)return -22;
    user_slot* s=&slots[handle.slot];
    if(!live(s) || s->info.handle.generation!=handle.generation)return -9;
    interactive_handle=handle;interactive_us=quantum_us;return 0;
}
uint32_t dihos_user_runtime_service(void){
    if(!enabled)return 0;
    uint64_t frequency;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(frequency));
    /* CNTFRQ is a 32-bit architectural frequency. Fail back to one step if
     * unavailable; never multiply an unbounded frequency or spin on no clock. */
    if(!frequency || frequency>UINT32_MAX)return runtime_step(1000)!=0;
    uint32_t budget_us=1000;
    if(interactive_handle.slot<DIHOS_USER_TASKS){user_slot* s=&slots[interactive_handle.slot];
        if(interactive_handle.generation && live(s) && s->info.handle.generation==interactive_handle.generation)budget_us=interactive_us;}
    uint64_t start=resource_clock(),budget=(frequency*budget_us+999999)/1000000;
    uint32_t steps=0;
    while(steps<64){
        uint64_t elapsed=resource_clock()-start;if(elapsed>=budget)break;
        uint64_t remaining_us=(budget-elapsed)*1000000/frequency;
        if(remaining_us<AA64_USER_TIMER_MIN_US)break;
        uint32_t quantum=(uint32_t)remaining_us;
        int result=runtime_step(quantum);
        if(!result)break;
        ++steps;
        /* Preemption, yield, log, image work and exit all return immediately.
         * The deadline is checked between steps; kernel service/cleanup cost
         * may exceed it. Thread/process cursors remain round-robin per step. */
        if(result!=1)break;
    }
    return steps;
}
int dihos_user_runtime_stop(dihos_process_handle handle){
    if(handle.slot>=DIHOS_USER_TASKS || aa64_user_slice_active())return -1;
    user_slot *s=&slots[handle.slot];
    if(s->info.handle.generation!=handle.generation || !s->info.state)return -2;
    if(live(s))finish_slot(s,DIHOS_USER_STOPPED);
    return 0;
}
void dihos_user_runtime_stop_all(void){for(unsigned i=0;i<DIHOS_USER_TASKS;++i)if(live(&slots[i]))dihos_user_runtime_stop(slots[i].info.handle);}
uint32_t dihos_user_runtime_snapshot(dihos_user_task_info *out,uint32_t capacity){
    if(!out)return 0;uint32_t count=0;
    for(unsigned i=0;i<DIHOS_USER_TASKS && count<capacity;++i)if(slots[i].info.state)out[count++]=slots[i].info;
    return count;
}
#else
int dihos_user_spawn_owned_browser_elf(void *data,uint64_t bytes,uint64_t pages,dihos_process_handle *out){(void)data;(void)bytes;(void)pages;(void)out;return -1;}
int dihos_user_spawn_owned_elf_with_grants(void *data,uint64_t bytes,uint64_t pages,uint64_t grants,dihos_process_handle *out){(void)data;(void)bytes;(void)pages;(void)grants;(void)out;return -1;}
int dihos_user_spawn_owned_elf_with_network(void *data,uint64_t bytes,uint64_t pages,uint32_t network,dihos_process_handle *out){(void)data;(void)bytes;(void)pages;(void)network;(void)out;return -1;}
int dihos_user_spawn_owned_elf(void *data,uint64_t bytes,uint64_t pages,dihos_process_handle *out){(void)data;(void)bytes;(void)pages;(void)out;return -1;}
int dihos_user_spawn_elf(const void *data,uint64_t bytes,dihos_process_handle *out){(void)data;(void)bytes;(void)out;return -1;}
int dihos_user_runtime_enable(void){return -1;}
int dihos_user_spawn_fixture(uint32_t kind,dihos_process_handle *out){(void)kind;(void)out;return -1;}
void dihos_user_runtime_poll(void){}
uint32_t dihos_user_runtime_service(void){return 0;}
int dihos_user_runtime_set_interactive(dihos_process_handle h,uint32_t us){(void)h;(void)us;return -38;}
int dihos_user_runtime_stop(dihos_process_handle handle){(void)handle;return -1;}
void dihos_user_runtime_stop_all(void){}
uint32_t dihos_user_runtime_snapshot(dihos_user_task_info *out,uint32_t cap){(void)out;(void)cap;return 0;}
#endif
