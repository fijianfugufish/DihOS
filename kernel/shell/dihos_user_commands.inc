/* Deliberately opt-in; normal desktop startup never changes GIC state. */
#include "asm/aa64_user_slice.h"
#include "process/dihos_user_loader.h"
static void dihos_print_dec_value(uint64_t value);
static int dihos_cmd_proc_run(dihos_shell_stage *stage){
    if(stage->positional_count!=1){terminal_error("usage: proc:run <ARM64 ELF path>");return -1;}
    int rc=dihos_user_load_start(stage->positional[0]);
    if(rc){terminal_print_inline("proc: queue failed: ");terminal_print_inline_hex32((uint32_t)rc);terminal_print("");return rc;}
    terminal_success("proc: ELF queued; proc:status reports loading and execution");return 0;
}
static int dihos_cmd_proc_run_net(dihos_shell_stage *stage){
    if(stage->positional_count!=1){terminal_error("usage: proc:run-net <ARM64 ELF path>");return -1;}
    int rc=dihos_user_load_start_network(stage->positional[0]);
    if(rc){terminal_error("proc: network-granted launch failed");return rc;}
    terminal_success("proc: ELF queued with explicit network grant");return 0;
}
static int dihos_cmd_proc_run_ui(dihos_shell_stage *stage){
    if(stage->positional_count!=1){terminal_error("usage: proc:run-ui <ARM64 ELF path>");return -1;}
    int rc=dihos_user_load_start_window(stage->positional[0]);
    if(rc){terminal_error("proc: window-granted launch failed");return rc;}
    terminal_success("proc: ELF queued with explicit owned-window grant");return 0;
}
static int dihos_cmd_proc_run_web(dihos_shell_stage *stage){
    if(stage->positional_count!=1){terminal_error("usage: proc:run-web <ARM64 ELF path>");return -1;}
    int rc=dihos_user_load_start_web(stage->positional[0]);
    if(rc){terminal_error("proc: browser-granted launch failed");return rc;}
    terminal_success("proc: ELF queued with explicit network and owned-window grants");return 0;
}
static int dihos_cmd_proc_status(dihos_shell_stage *stage)
{
    (void)stage;
    dihos_user_load_status loading;dihos_user_load_status_get(&loading);
    if(loading.state){
        terminal_print_inline("proc: ELF load state/error/bytes read: ");terminal_print_inline_hex32(loading.state);terminal_print_inline(" ");terminal_print_inline_hex32((uint32_t)loading.error);terminal_print_inline(" ");terminal_print_inline_hex64(loading.read_bytes);terminal_print("");
        if(loading.total_bytes){
            terminal_print_inline("proc: file read ");dihos_print_dec_value(loading.read_bytes/1024);terminal_print_inline(" / ");dihos_print_dec_value(loading.total_bytes/1024);terminal_print_inline(" KiB (");dihos_print_dec_value(loading.read_bytes*100/loading.total_bytes);terminal_print("%)");
            terminal_print_inline("proc: disk read calls / last ms / max ms: ");dihos_print_dec_value(loading.read_calls);terminal_print_inline(" / ");dihos_print_dec_value(loading.last_read_ms);terminal_print_inline(" / ");dihos_print_dec_value(loading.max_read_ms);terminal_print("");
        }
    }
    cpu_info_snapshot cpu;cpu_info_get(&cpu);
    terminal_print_inline("proc: GIC version / timer IRQ / flags: ");
    terminal_print_inline_hex32(cpu.gic_version);terminal_print_inline(" ");
    terminal_print_inline_hex32(cpu.virtual_timer_irq);terminal_print_inline(" ");
    terminal_print_inline_hex32(cpu.virtual_timer_flags);terminal_print("");
    terminal_print_inline("proc: distributor: ");terminal_print_inline_hex64(cpu.gic_distributor);terminal_print("");
    terminal_print_inline("proc: redistributor: ");terminal_print_inline_hex64(cpu.gic_redistributor);terminal_print("");
    terminal_print_inline("proc: redistributor bytes: ");
    terminal_print_inline_hex64(cpu.gic_redistributor_bytes);terminal_print("");
#if defined(__aarch64__)
    aa64_user_timer_diagnostic timer;aa64_user_timer_get_diagnostic(&timer);
    terminal_print_inline("proc: driver redistributor: ");
    terminal_print_inline_hex64(timer.config.redistributor);terminal_print("");
    terminal_print_inline("proc: timer ready/current core/initialized core: ");
    terminal_print_inline_hex32(timer.was_ready);terminal_print_inline(" ");
    terminal_print_inline_hex64(timer.current_core);terminal_print_inline(" ");
    terminal_print_inline_hex64(timer.initialized_core);terminal_print("");
#endif
    dihos_user_task_info tasks[DIHOS_USER_TASKS];
    uint32_t count=dihos_user_runtime_snapshot(tasks,DIHOS_USER_TASKS);
    static const char *states[]={"empty","ready","sleeping","exited","faulted","stopped","loading"};
    if(!count)terminal_print(loading.state==1 || loading.state==2?"proc: executable is still being read; no user task yet":"proc: no EL0 tasks running");
    for(uint32_t i=0;i<count;++i){
        const dihos_user_task_info *p=&tasks[i];
        if(p->state==DIHOS_USER_LOADING || p->admission_result){terminal_print_inline("proc: image phase/work/span: ");terminal_print_inline_hex32(p->admission_step);terminal_print_inline(" ");terminal_print_inline_hex64(p->load_done);terminal_print_inline(" ");terminal_print_inline_hex64(p->load_total);terminal_print("");}
        terminal_print_inline("proc: slot/generation/kind ");terminal_print_inline_hex32(p->handle.slot);terminal_print_inline(" ");terminal_print_inline_hex32(p->handle.generation);terminal_print_inline(" ");terminal_print_inline_hex32(p->kind);terminal_print_inline(" state=");terminal_print(p->state<7?states[p->state]:"invalid");
        terminal_print_inline("proc: slices/preemptions/counter ");terminal_print_inline_hex64(p->slices);terminal_print_inline(" ");terminal_print_inline_hex64(p->preemptions);terminal_print_inline(" ");terminal_print_inline_hex64(p->counter);terminal_print("");
        if(p->sacx_calls){terminal_print_inline("proc: SACX calls/grants/last result: ");terminal_print_inline_hex64(p->sacx_calls);terminal_print_inline(" ");terminal_print_inline_hex64(p->granted_capabilities);terminal_print_inline(" ");terminal_print_inline_hex64((uint64_t)p->sacx_last_result);terminal_print("");}
        if(p->last_log[0]){terminal_print_inline("proc: SACX log: ");terminal_print(p->last_log);}
        if(p->memory_peak){terminal_print_inline("proc: heap reserved/committed/peak: ");terminal_print_inline_hex64(p->memory_reserved);terminal_print_inline(" ");terminal_print_inline_hex64(p->memory_committed);terminal_print_inline(" ");terminal_print_inline_hex64(p->memory_peak);terminal_print("");}
        if(p->relocations){terminal_print_inline("proc: relative relocations: ");terminal_print_inline_hex32(p->relocations);terminal_print("");}
        if(p->threads_created || p->tls_bytes){
            terminal_print_inline("proc: threads live/created/exited/joins: ");terminal_print_inline_hex32(p->threads_live);terminal_print_inline(" ");terminal_print_inline_hex64(p->threads_created);terminal_print_inline(" ");terminal_print_inline_hex64(p->threads_exited);terminal_print_inline(" ");terminal_print_inline_hex64(p->thread_joins);terminal_print("");
            terminal_print_inline("proc: TLS bytes/waits/wakes/timeouts: ");terminal_print_inline_hex32(p->tls_bytes);terminal_print_inline(" ");terminal_print_inline_hex64(p->thread_waits);terminal_print_inline(" ");terminal_print_inline_hex64(p->thread_wakes);terminal_print_inline(" ");terminal_print_inline_hex64(p->thread_timeouts);terminal_print("");
            terminal_print_inline("proc: held threads/inspector/detaches: ");terminal_print_inline_hex32(p->threads_held);terminal_print_inline(" ");terminal_print_inline_hex64(p->inspection_owner);terminal_print_inline(" ");terminal_print_inline_hex64(p->thread_detaches);terminal_print("");
        }
        if(p->fault_thread){terminal_print_inline("proc: faulting thread: ");terminal_print_inline_hex64(p->fault_thread);terminal_print("");}
        if(p->state==DIHOS_USER_DONE){terminal_print_inline("proc: exit status: ");terminal_print_inline_hex64(p->exit_status);terminal_print("");}
        if(p->code_va){terminal_print_inline(p->backing_alias?"proc: owned backing code/stack: ":"proc: spare alias code/stack: ");terminal_print_inline_hex64(p->code_va);terminal_print_inline(" ");terminal_print_inline_hex64(p->stack_va);terminal_print("");}
        if(p->admission_result){
            static const char *steps[]={"none","code allocation","stack allocation","private VM creation","code translation","stack translation","unmapped address search","code mapping","stack mapping","process ready","process running"};
            terminal_print_inline("proc: admission failed: ");terminal_print(p->admission_step<11?steps[p->admission_step]:"unknown");
            terminal_print_inline("proc: underlying error: ");terminal_print_inline_hex32((uint32_t)p->admission_result);terminal_print("");
        }else if(p->state==DIHOS_USER_FAILED){
            terminal_print_inline("proc: PC/ESR/FAR ");terminal_print_inline_hex64(p->pc);terminal_print_inline(" ");terminal_print_inline_hex64(p->esr);terminal_print_inline(" ");terminal_print_inline_hex64(p->far);terminal_print("");
            terminal_print_inline("proc: fault walk level/TCR: ");terminal_print_inline_hex32((uint32_t)p->fault_level);terminal_print_inline(" ");terminal_print_inline_hex64(p->tcr);terminal_print("");
            terminal_print_inline("proc: active/expected root: ");terminal_print_inline_hex64(p->hardware_root);terminal_print_inline(" ");terminal_print_inline_hex64(p->expected_root);terminal_print("");
            terminal_print_inline("proc: hardware PAR / SCTLR: ");terminal_print_inline_hex64(p->hardware_par);terminal_print_inline(" ");terminal_print_inline_hex64(p->sctlr);terminal_print("");
            for(unsigned level=0;level<4;++level){terminal_print_inline("proc: descriptor level ");terminal_print_inline_hex32(level);terminal_print_inline(" = ");terminal_print_inline_hex64(p->fault_walk[level]);terminal_print("");}
        }
    }
    return 0;
}
static int dihos_cmd_proc_test(dihos_shell_stage *stage)
{
    (void)stage;
    int rc=dihos_user_runtime_enable();
    if(rc){
        terminal_error("proc: EL0 timer preflight unavailable or failed");
        terminal_print_inline("proc: error=");terminal_print_inline_hex32((uint32_t)rc);terminal_print("");
        const char *reason="unknown runtime failure";
        switch(rc){
        case -200:reason="finite probe admission failed; proc:status reports the step and error";break;
        case -110:reason="missing timer configuration pointer";break;
        case -111:reason="zero distributor address received by timer driver";break;
        case -112:reason="zero redistributor address received by timer driver";break;
        case -113:reason="timer driver received an invalid PPI";break;
        case -114:reason="timer driver received redistributor length outside 128 KiB..16 MiB";break;
        case -115:reason="distributor address is not 64 KiB aligned";break;
        case -116:reason="redistributor address is not 4 KiB aligned";break;
        case -117:reason="redistributor address plus length overflows";break;
        case -118:reason="timer initialized on a different CPU";break;
        case -100:reason="CPU/ACPI information not initialized";break;
        case -101:reason="unsupported GIC version";break;
        case -102:reason="virtual timer IRQ is not a PPI (16..31)";break;
        case -1:reason="invalid GIC addresses, alignment, range or boot CPU";break;
        case -2:reason="GIC system interface, version or timer frequency unsupported";break;
        case -3:reason="timer or timer interrupt already enabled";break;
        case -4:reason="GIC mapping, register access or update timeout";break;
        case -5:reason="boot redistributor discovery or fixture allocation failed";break;
        case -6:reason="finite EL0 probe did not receive the expected timer interrupt";break;
        }
        terminal_print(reason);return rc;
    }
    dihos_user_runtime_stop_all();
    for(uint32_t kind=DIHOS_USER_LOOP;kind<=DIHOS_USER_SLEEP;++kind){
        dihos_process_handle handle;
        if(dihos_user_spawn_fixture(kind,&handle)){dihos_user_runtime_stop_all();terminal_error("proc: fixture admission failed; stopped all test tasks");return -1;}
    }
    terminal_success("proc: loop, exit, fault and sleeping EL0 tasks started");
    terminal_print("proc: use the desktop, then proc:status; proc:stop cancels the loop");
    return 0;
}
static int dihos_cmd_proc_stop(dihos_shell_stage *stage)
{
    (void)stage;dihos_user_load_cancel();dihos_user_runtime_stop_all();terminal_success("proc: stopped user tasks and pending load; released their pages");return 0;
}
