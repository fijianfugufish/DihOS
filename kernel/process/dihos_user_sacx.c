#include "process/dihos_user_sacx.h"
#include "system/entropy.h"
#include "system/dihos_time.h"
#include "../../sdk/sacx/include/sacx_user_resources.h"
#include "../../sdk/sacx/include/sacx_user_network.h"
#include "../../sdk/sacx/include/sacx_user_http_request.h"
#include "../../sdk/sacx/include/sacx_user_window.h"
#include "../../sdk/sacx/include/sacx_browser_session.h"
void dihos_user_sacx_service(const aarch64_user_vm *vm,uint64_t grants,uint64_t version,
    uint64_t operation,const uint64_t a[3],uint64_t now,dihos_user_sacx_reply *out){
    if(!out)return;*out=(dihos_user_sacx_reply){0};
    if(version!=SACX_USER_ABI_VERSION){out->result=-93;return;}
    switch(operation){
    case SACX_USER_QUERY:{
        sacx_user_info info={SACX_USER_ABI_VERSION,sizeof(info),0x3ffffffffffull,grants,SACX_USER_BUFFER_MAX,0};
        if(a[1]!=sizeof(info)){out->result=-22;break;}
        out->result=aarch64_user_vm_copy_out(vm,a[0],&info,sizeof(info));break;
    }
    case SACX_USER_YIELD:break;
    case SACX_USER_PROCESS_SELF:
        if(a[0] || a[1] || a[2])out->result=-22;
        else out->action=DIHOS_SACX_IDENTITY;
        break;
    case SACX_USER_RESOURCES:
        if((grants&(SACX_USER_CAP_CLOCK|SACX_USER_CAP_MEMORY))!=(SACX_USER_CAP_CLOCK|SACX_USER_CAP_MEMORY))out->result=-1;
        else if(a[1]!=sizeof(sacx_user_resources) || a[2])out->result=-22;
        else {
            out->result=aarch64_user_vm_check_buffer(vm,a[0],a[1],1);
            if(!out->result)out->action=DIHOS_SACX_RESOURCES;
        }
        break;
    case SACX_USER_EXIT:out->action=DIHOS_SACX_EXIT;out->argument=a[0];break;
    case SACX_USER_MONOTONIC_MS:
        out->result=(grants&SACX_USER_CAP_CLOCK)?(int64_t)now:-1;break;
    case SACX_USER_UTC_MS:{
        if(!(grants&SACX_USER_CAP_CLOCK)){out->result=-1;break;}
        if(a[0] || a[1] || a[2]){out->result=-22;break;}
        uint64_t ms;out->result=dihos_time_utc_ms(&ms)?-61:(int64_t)ms;break;
    }
    case SACX_USER_SLEEP_MS:
        if(!(grants&SACX_USER_CAP_CLOCK)){out->result=-1;break;}
        if(a[0]>60000){out->result=-22;break;}
        out->action=DIHOS_SACX_SLEEP;out->argument=a[0];break;
    case SACX_USER_LOG:
        if(!(grants&SACX_USER_CAP_LOG)){out->result=-1;break;}
        if(a[1]>SACX_USER_BUFFER_MAX){out->result=-7;break;}
        out->result=aarch64_user_vm_copy_in(vm,out->log,a[0],a[1]);
        if(!out->result){out->log_bytes=(uint32_t)a[1];out->log[a[1]]=0;}
        break;
    case SACX_USER_RANDOM:{
        if(!(grants&SACX_USER_CAP_RANDOM)){out->result=-1;break;}
        if(a[2] || !a[1]){out->result=-22;break;}
        if(a[1]>DIHOS_ENTROPY_MAX){out->result=-7;break;}
        out->result=aarch64_user_vm_check_buffer(vm,a[0],a[1],1);
        if(out->result)break;
        uint8_t bytes[DIHOS_ENTROPY_MAX];
        out->result=dihos_entropy_fill(bytes,(size_t)a[1]);
        if(!out->result)out->result=aarch64_user_vm_copy_out(vm,a[0],bytes,a[1]);
        for(unsigned i=0;i<sizeof(bytes);++i)((volatile uint8_t*)bytes)[i]=0;
        break;
    }
    case SACX_USER_WINDOW_CREATE:case SACX_USER_WINDOW_WRITE:case SACX_USER_WINDOW_PRESENT:
    case SACX_USER_WINDOW_EVENT:case SACX_USER_WINDOW_CLOSE:case SACX_USER_WINDOW_REGION:
        if(!(grants&SACX_USER_CAP_SURFACE))out->result=-1;
        else out->action=DIHOS_SACX_WINDOW;
        break;
    case SACX_USER_BROWSER_SESSION:
        if(!(grants&SACX_USER_CAP_SURFACE))out->result=-1;
        else if(a[0]>0xffffffffull || a[2]>4096)out->result=-22;
        else out->action=DIHOS_SACX_BROWSER;
        break;
    case SACX_USER_HTTP_START:case SACX_USER_HTTP_POLL:case SACX_USER_HTTP_READ:case SACX_USER_HTTP_CLOSE:
    case SACX_USER_HTTP_HEADERS:case SACX_USER_HTTP_START_MANUAL:case SACX_USER_HTTP_REQUEST:case SACX_USER_HTTP_SEND:
        if(!(grants&SACX_USER_CAP_NETWORK))out->result=-1;
        else out->action=DIHOS_SACX_NETWORK;
        break;
    case SACX_USER_SPARSE_RESERVE:case SACX_USER_SPARSE_RELEASE:
    case SACX_USER_MEMORY_RESERVE:case SACX_USER_MEMORY_COMMIT:
    case SACX_USER_MEMORY_PROTECT:case SACX_USER_MEMORY_RELEASE:case SACX_USER_MEMORY_DECOMMIT:
        if(!(grants&SACX_USER_CAP_MEMORY))out->result=-1;
        else out->action=DIHOS_SACX_MEMORY;
        break;
    case SACX_USER_THREAD_CREATE:case SACX_USER_THREAD_SELF:case SACX_USER_THREAD_EXIT:
    case SACX_USER_THREAD_JOIN:case SACX_USER_WAIT32:case SACX_USER_WAKE32:
    case SACX_USER_THREAD_PAUSE:case SACX_USER_THREAD_RESUME:case SACX_USER_THREAD_STACK:
    case SACX_USER_THREAD_DETACH:
        if(!(grants&SACX_USER_CAP_THREADS))out->result=-1;
        else out->action=DIHOS_SACX_THREAD;
        break;
    default:out->result=-38;break;
    }
}
