#include <assert.h>
#include <stdio.h>
#define DIHOS_PMEM_KEEP_FIRMWARE 1
#include "../kernel/memory/pmem.c"
volatile uint32_t *g_fb32;
const boot_info *k_bootinfo_ptr;
int main(void){
    efi_mdesc map[3]={0};
    map[0].Type=EFI_BOOT_SERVICES_CODE;map[0].PhysicalStart=0x10000;map[0].NumberOfPages=16;
    map[1].Type=EFI_BOOT_SERVICES_DATA;map[1].PhysicalStart=0x20000;map[1].NumberOfPages=16;
    map[2].Type=EFI_CONVENTIONAL_MEMORY;map[2].PhysicalStart=0x100000;map[2].NumberOfPages=16;
    boot_info bi={0};bi.mmap=(uintptr_t)map;bi.mmap_size=sizeof(map);bi.mmap_desc_size=sizeof(map[0]);
    pmem_init(&bi);
    assert(!is_reclaimable_type(EFI_BOOT_SERVICES_CODE));
    assert(!is_reclaimable_type(EFI_BOOT_SERVICES_DATA));
    assert(is_reclaimable_type(EFI_CONVENTIONAL_MEMORY));
    for(unsigned i=0;i<16;++i)assert((uintptr_t)pmem_alloc_pages(1)==0x100000+i*4096);
    assert(!pmem_alloc_pages(1));
    puts("PASS: allocations never reuse live ARM64 firmware stack/table memory; conventional RAM stays usable");
}
