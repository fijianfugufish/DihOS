#include <assert.h>
#include <stdio.h>
#include "../kernel/memory/pmem.c"
volatile uint32_t *g_fb32;
const boot_info *k_bootinfo_ptr;
int main(void)
{
    for (uint32_t i=0;i<64;++i) range_add_to(pool_hi,&pool_hi_count,0x100000000ull+i*0x100000,0x100000);
    assert(pool_hi_count==1 && pool_hi[0].len==64*0x100000ull);
    assert(pmem_largest_free_pages()==64*256);
    for (uint32_t pass=0;pass<1000;++pass) {
        void *blocks[32];
        for (uint32_t i=0;i<32;++i) { blocks[i]=pmem_alloc_pages(16+i);assert(blocks[i]); }
        assert(pmem_largest_free_pages()<64*256);
        for (uint32_t i=0;i<32;i+=2) pmem_free_pages(blocks[i],16+i);
        for (uint32_t i=1;i<32;i+=2) pmem_free_pages(blocks[i],16+i);
        assert(pool_hi_count==1 && pool_hi[0].len==64*0x100000ull);
    }
    puts("PASS: adjacent firmware/free memory coalescing; 32000 allocations/frees without losing range slots");
    return 0;
}
