#include "../tools/jsc_dihos/vm_failure_report.h"
#include <cassert>
#include <cstring>
#include <climits>
int main(){
    char out[257];
    DihOSVM::describe(out,{"commit",-12,0x12345000,65536,0xabcdef,10,20,30,40});
    assert(std::strstr(out,"WTF VM commit rc=-12") && std::strstr(out,"bytes=0x0000000000010000"));
    assert(std::strstr(out,"committed=0x000000000000001e") && std::strlen(out)<=256);
    DihOSVM::describe(out,{"decommit",INT_MIN,UINTPTR_MAX,SIZE_MAX,UINTPTR_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX});
    assert(std::strstr(out,"rc=-2147483648") && std::strlen(out)<=256);
    char large[1024];std::memset(large,'a',1023);large[1023]=0;
    DihOSVM::describe(out,{large,-14,0,0,0,0,0,0,0});assert(std::strlen(out)==256);
}
