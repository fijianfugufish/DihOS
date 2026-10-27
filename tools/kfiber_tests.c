#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "system/kfiber.h"
static int visits;
static void request(void *argument)
{
    volatile double retained=1.25;
    for (int i=0;i<10000;++i) {
        assert((int *)argument==&visits);
        ++visits;retained+=0.5;kfiber_yield();
        assert(retained==1.25+(i+1)*0.5);
    }
}
int main(void)
{
    kfiber fiber;void *stack=malloc(65536);assert(stack);
    assert(kfiber_init(&fiber,stack,1,request,&visits)<0);
    assert(!kfiber_init(&fiber,stack,65536,request,&visits));
    for (int i=0;i<10000;++i) { assert(!kfiber_resume(&fiber));assert(visits==i+1); }
    assert(kfiber_resume(&fiber)==1 && fiber.state==KFIBER_DONE);
    assert(kfiber_resume(&fiber)<0);free(stack);
    puts("PASS: 10000 same-core continuation suspensions retain stack and floating-point state; completion and invalid arguments");
}
