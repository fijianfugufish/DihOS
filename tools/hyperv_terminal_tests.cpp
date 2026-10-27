#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int allocations, constructors, fail_next;
static void *blocks[8];
static void *pmem_alloc_pages(uint64_t pages)
{
    if (fail_next) { fail_next=0; return 0; }
    assert(allocations<8);
    void *p=malloc((size_t)pages*4096); assert(p);
    memset(p,0xa5,(size_t)pages*4096);
    blocks[allocations++]=p; return p;
}
class Terminal {
public:
    uint8_t initial_zero[8200];
    Terminal() {
        for (size_t i=0;i<sizeof(initial_zero);++i) assert(!initial_zero[i]);
        ++constructors;
    }
};
static const int MAX_TERMINALS=6;
#include "../kernel/terminal/terminal_slots.inc"
int main(void)
{
    assert(constructors==1 && allocations==0);
    assert(terminal_slot_create(0)==&g_primary_terminal && allocations==0);
    assert(!terminal_slot_create(6) && allocations==0);
    fail_next=1; assert(!terminal_slot_create(1) && !g_terminals[1]);
    assert(constructors==1 && allocations==0);
    for (uint32_t i=1;i<6;++i) {
        Terminal *slot=terminal_slot_create(i); assert(slot && slot!=&g_primary_terminal);
        assert(constructors==(int)i+1 && allocations==(int)i);
        assert(terminal_slot_create(i)==slot && allocations==(int)i);
    }
    for (int i=0;i<allocations;++i) free(blocks[i]);
    puts("PASS: one resident terminal, lazy secondary construction, zeroed storage, allocation failure recovery, slot reuse and bounds");
}
