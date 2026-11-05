#include <assert.h>
#include <stdio.h>
#include "asm/aa64_user_slice.h"
int main(void){
    aa64_user_timer_config c={0xffff0000,0xeffee000,0x20000,20,0};
    assert(aa64_user_timer_validate(&c)==0);
    assert(aa64_user_timer_validate(0)==-110);
    c.distributor=0;assert(aa64_user_timer_validate(&c)==-111);c.distributor=0xffff0000;
    c.redistributor=0;assert(aa64_user_timer_validate(&c)==-112);c.redistributor=0xeffee000;
    c.irq=32;assert(aa64_user_timer_validate(&c)==-113);c.irq=20;
    c.redistributor_bytes=0x1ffff;assert(aa64_user_timer_validate(&c)==-114);
    c.redistributor_bytes=0x1000001;assert(aa64_user_timer_validate(&c)==-114);c.redistributor_bytes=0x20000;
    c.distributor++;assert(aa64_user_timer_validate(&c)==-115);c.distributor--;
    c.redistributor++;assert(aa64_user_timer_validate(&c)==-116);c.redistributor--;
    c.redistributor=0xffffffffffff0000ull;assert(aa64_user_timer_validate(&c)==-117);
    puts("PASS: guest-reported timer configuration accepted; every invalid field identified separately");
}
