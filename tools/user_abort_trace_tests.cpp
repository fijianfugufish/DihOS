#include "../sdk/sacx/user_runtime/include/dihos_user_abort_trace.h"
#include <cassert>
#include <cstring>
#include <initializer_list>

int main(){
    char message[257];
    alignas(16) uintptr_t frames[16]{};
    const uintptr_t begin=reinterpret_cast<uintptr_t>(frames),end=begin+sizeof(frames);
    for(unsigned i=0;i<7;++i){frames[i*2]=begin+(i+1)*16;frames[i*2+1]=0x1000+i;}
    dihos_user_abort_message(message,"optional operator* called on a disengaged value",0x1234,begin,begin,end);
    assert(std::strstr(message,"optional operator*") && std::strstr(message,"0000000000001234"));
    assert(std::strstr(message,"0000000000001005") && std::strlen(message)<=256);
    // An untrusted chain cannot escape the owned stack, loop, or read a partial record.
    frames[0]=1;dihos_user_abort_message(message,"cycle",0x1234,begin,begin,end);
    assert(std::strlen(message)<200);
    for(uintptr_t frame: {uintptr_t(1),begin+1,end-8,end,uintptr_t(-16)}){
        dihos_user_abort_message(message,"bad frame",0x1234,frame,begin,end);
        assert(std::strstr(message,"0000000000001234"));
    }
    frames[0]=begin;dihos_user_abort_message(message,nullptr,0x1234,begin,begin,end);
    assert(std::strlen(message)<200);
    char long_reason[1024];std::memset(long_reason,'x',sizeof(long_reason));long_reason[1023]=0;
    dihos_user_abort_message(message,long_reason,0x1234,0,0,0);assert(std::strlen(message)<=256);
}
