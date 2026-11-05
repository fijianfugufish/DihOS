/* EFI 2.11 37.5: caller-owned buffer; boot services only. */
typedef struct dihos_efi_rng dihos_efi_rng;
typedef EFI_STATUS (*dihos_efi_rng_get)(dihos_efi_rng*,const EFI_GUID*,UINTN,uint8_t*);
struct dihos_efi_rng {void *get_info;dihos_efi_rng_get get_rng;};
static void stage2_capture_rng(EFI_SYSTEM_TABLE *st,boot_info *bi){
    static const EFI_GUID protocol={0x3152bca5,0xeade,0x433d,{0x86,0x2e,0xc0,0x1c,0xdc,0x29,0x1f,0x44}};
    static const EFI_GUID raw={0xe43176d7,0xb6e8,0x4827,{0xb7,0x84,0x7f,0xfd,0xc4,0xb6,0x85,0x61}};
    const EFI_STATUS unsupported=0x8000000000000003ull;
    dihos_efi_rng *rng=0;bi->boot_rng_flags=0;bi->boot_rng_status=unsupported;
    for(unsigned i=0;i<48;++i)bi->boot_rng_seed[i]=0;
    if(!st || !st->BootServices || !BsLocate(st->BootServices))return;
    EFI_STATUS status=BsLocate(st->BootServices)(&protocol,0,(void**)&rng);
    if(!status && rng && rng->get_rng){
        status=rng->get_rng(rng,&raw,48,bi->boot_rng_seed);
        if(!status)bi->boot_rng_flags=3;
        else if(status==unsupported){
            /* Default EFI RNG algorithms must provide >=256-bit security. */
            for(unsigned i=0;i<48;++i)bi->boot_rng_seed[i]=0;
            status=rng->get_rng(rng,0,48,bi->boot_rng_seed);
            if(!status)bi->boot_rng_flags=1;
        }
    }else if(!status)status=unsupported;
    bi->boot_rng_status=status;
    if(status)for(unsigned i=0;i<48;++i)((volatile uint8_t*)bi->boot_rng_seed)[i]=0;
}
