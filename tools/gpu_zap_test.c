#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu/gpu_zap.h"
#include "gpu/gpu_qcom_scm.h"

static unsigned calls, failures, fail_stage, fail_kind;
static unsigned map_calls;
static uint64_t meta_phys, code_phys;
static unsigned char file_data[16384];
static gpu_zap_layout layout;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(0)

void terminal_print(const char *s) { (void)s; }
void terminal_print_inline_hex64(uint64_t x) { (void)x; }
void terminal_flush_log(void) {}
void asm_dma_clean_range(const void *p,uint64_t n) { (void)p; (void)n; }
void asm_dma_invalidate_range(const void *p,uint64_t n) { (void)p; (void)n; }
int mmio_map_normal_nc_identity(uint64_t p,uint64_t n)
{
    CHECK(p && n && !((p|n)&4095));
    ++map_calls;
    return fail_stage == 6 || (fail_stage == 7 && map_calls == 2) ? -2 : 0;
}
int gpu_buffer_alloc(gpu_buffer *b,uint64_t bytes,uint32_t flags)
{
    void *p=calloc(1,(size_t)bytes+4095);
    if (!p) return -1;
    uintptr_t v=((uintptr_t)p+4095)&~(uintptr_t)4095;
    *b=(gpu_buffer){(void *)v,v,0,bytes,(bytes+4095)/4096,flags};
    return 0;
}
const gpu_firmware_blob *gpu_firmware_find(const gpu_firmware_set *s,gpu_firmware_role role)
{ return s && s->blob_count && s->blobs[0].role==role ? &s->blobs[0] : NULL; }

int asm_aa64_try_smc6(uint32_t imm,uint64_t *x0,uint64_t *x1,uint64_t *x2,
                     uint64_t *x3,uint64_t *x4,uint64_t *x5,uint64_t *esr)
{
    ++calls; CHECK(!imm && !*x5);
    static const uint64_t ids[]={0,0x42000601,0x42000207,0x42000201,0x42000202,0x42000205};
    CHECK(calls<=5 && *x0==ids[calls]);
    CHECK(*x2==(calls==1 ? 0x02000207 : 13));
    CHECK(*x1==(calls==3 ? 0x82 : calls==4 ? 3 : 1));
    if (calls==3) {
        meta_phys=*x3; CHECK(!(meta_phys&4095));
        CHECK(!memcmp((void *)(uintptr_t)meta_phys,file_data,layout.header_bytes));
        CHECK(!memcmp((unsigned char *)(uintptr_t)meta_phys+layout.header_bytes,
                      file_data+layout.hash_offset,layout.hash_bytes));
    }
    if (calls==4) { code_phys=*x3; CHECK(!(code_phys&0xfffff)); CHECK(*x4==layout.memory_bytes); }
    if (calls==5) {
        CHECK(!memcmp((void *)(uintptr_t)code_phys,file_data+layout.code_offset,layout.code_bytes));
        for (unsigned i=layout.code_bytes;i<layout.memory_bytes;i++) CHECK(!((unsigned char *)(uintptr_t)code_phys)[i]);
    }
    *x0=0; *x1=calls<=2 ? 1 : 0; *esr=0;
    if (calls==fail_stage) {
        if (fail_kind==1) *x0=~0ull;
        else if (fail_kind==2) { *esr=0x1234; return -1; }
        else *x1=calls<=2 ? 0 : 0xdead;
    }
    return 0;
}

int main(int argc,char **argv)
{
    fail_stage=argc>1 ? (unsigned)atoi(argv[1]) : 0;
    fail_kind=argc>2 ? (unsigned)atoi(argv[2]) : 0;
    FILE *f=fopen(argc>3 ? argv[3] : "OS/Firmware/adreno-x1-85/upstream/gen70500_zap.mbn","rb");
    if (!f) return 2;
    size_t n=fread(file_data,1,sizeof(file_data),f); fclose(f);
    CHECK(!gpu_zap_parse(file_data,n,&layout));
    CHECK(layout.header_bytes==148 && layout.hash_bytes==0xf38 && layout.code_bytes==0x430 && layout.memory_bytes==4096);
    for (unsigned i=0;i<148;i++) { gpu_zap_layout bad; CHECK(gpu_zap_parse(file_data,i,&bad)!=0); }
    const unsigned offsets[]={0,4,5,6,16,18,20,28,40,42,44,52,56,68,76,84,92,96,108,112,116,140};
    for (unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        unsigned at=offsets[i]; unsigned char save=file_data[at]; gpu_zap_layout bad;
        file_data[at]^=0xff; CHECK(gpu_zap_parse(file_data,n,&bad)!=0); file_data[at]=save;
    }
    gpu_firmware_set s={0}; s.blob_count=1; s.blobs[0].role=GPU_FIRMWARE_SECURE;
    s.blobs[0].buffer.cpu=file_data; s.blobs[0].buffer.size_bytes=n;
    int rc=gpu_zap_start(&s);
    CHECK(fail_stage ? rc!=0 : rc==0);
    CHECK(calls==(fail_stage >= 6 ? 2 : fail_stage ? fail_stage : 5));
    unsigned count=calls; rc=gpu_zap_start(&s);
    CHECK(fail_stage ? rc!=0 : rc==0); CHECK(calls==count);
    printf("zap stage %u kind %u: %s (ELF bounds, PAS args, metadata, relocation, retry)\n",fail_stage,fail_kind,failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
