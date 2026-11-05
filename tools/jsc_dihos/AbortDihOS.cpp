// Test-executable diagnostics only. Preserve abort's process-termination contract
// while retaining the caller address without depending on a working formatter.
#include "sacx_user_api.h"
#include "sacx_user_thread_context.h"
extern "C" int dihos_user_main(const sacx_api*);
extern "C" [[noreturn]] void __wrap_abort(){
    // Walk only aligned, forward frames inside the kernel-reported own stack.
    auto frame=static_cast<const uintptr_t*>(__builtin_frame_address(0));
    const uintptr_t floor=reinterpret_cast<uintptr_t>(frame);
    sacx_user_stack_bounds bounds{};
    const bool bounded=!sacx_user_thread_stack(sacx_user_thread_self(),&bounds);
    for(unsigned depth=0;bounded && depth<8 && frame;++depth){
        uintptr_t address=reinterpret_cast<uintptr_t>(frame);
        if((address&15) || address<floor || address<bounds.begin ||
            address>bounds.end || bounds.end-address<2*sizeof(uintptr_t))break;
        char trace[]="JSC frame=0x0000000000000000";
        constexpr char hex[]="0123456789abcdef";
        for(unsigned i=0;i<16;++i)trace[12+i]=hex[(frame[1]>>((15-i)*4))&15];
        sacx_user_log(trace);
        uintptr_t next=frame[0];
        if(next<=address)break;
        frame=reinterpret_cast<const uintptr_t*>(next);
    }
    const uintptr_t values[]={reinterpret_cast<uintptr_t>(__builtin_return_address(0)),
        reinterpret_cast<uintptr_t>(&dihos_user_main)};
    char message[]="JSC abort caller=0x0000000000000000 main=0x0000000000000000";
    const unsigned positions[]={sizeof("JSC abort caller=0x")-1,
        sizeof("JSC abort caller=0x0000000000000000 main=0x")-1};
    constexpr char digits[]="0123456789abcdef";
    for(unsigned field=0;field<2;++field)
        for(unsigned i=0;i<16;++i)message[positions[field]+i]=digits[(values[field]>>((15-i)*4))&15];
    sacx_user_exit(191,message);
}
