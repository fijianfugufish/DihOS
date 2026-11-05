#include <assert.h>
#include <stdio.h>
#include "net/tcp_receive.h"
int main(void){
    assert(dihos_tcp_receive_skip(100,100,20)==0);
    assert(dihos_tcp_receive_skip(100,90,20)==10);
    assert(dihos_tcp_receive_skip(100,80,20)==20);
    assert(dihos_tcp_receive_skip(100,110,20)==20);
    assert(dihos_tcp_receive_skip(4,0xfffffffc,16)==8);
    assert(dihos_tcp_receive_skip(0xfffffffc,4,16)==16);
    assert(dihos_tcp_receive_fin(110,90,20));
    assert(!dihos_tcp_receive_fin(100,110,20));
    assert(!dihos_tcp_receive_fin(120,90,20));
    assert(DIHOS_TCP_RECEIVE_WINDOW<=16384);
    puts("PASS: contiguous TCP suffix, duplicates, future segments, sequence wrap and FIN gap rejection");
}
