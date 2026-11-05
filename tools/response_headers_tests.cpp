#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include "webcore_dihos/platform/ResponseHeadersDihOS.h"
static int parse(const char* text){size_t at=0,n=strlen(text);int fields=0;for(;;){DihosResponseField f{};int r=dihos_response_field((const uint8_t*)text,n,at,&f);if(r)return r==1?fields:-1;++fields;at=f.next;}}
int main(){
    assert(parse("x-analytics: \r\nContent-Length: 0\r\n\r\n")==2);
    assert(parse("Empty:\r\nTabs:\t \t\r\nLatin1: \xe9\r\nSet-Cookie: one=1\r\nSet-Cookie: two=2\r\n\r\n")==5);
    for(auto bad:{"Bad Header: x\r\n\r\n",": no-name\r\n\r\n","Fold: x\r\n y\r\n\r\n","X: a\001b\r\n\r\n","X: a\nb\r\n\r\n","X: value\r\n","X: value\r\n\r\nextra"})assert(parse(bad)==-1);
    DihosResponseField f{};const char* h="X:\t value \t\r\n\r\n";assert(!dihos_response_field((const uint8_t*)h,strlen(h),0,&f));assert(f.valueBytes==5 && !memcmp(h+f.value,"value",5));
    puts("PASS: empty/whitespace-only response values, Latin-1, duplicate headers, OWS trimming; invalid names, controls, folds and incomplete/trailing framing rejected");
}
