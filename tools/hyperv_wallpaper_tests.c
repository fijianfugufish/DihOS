#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../kernel/kwrappers/kimg.c"
static FILE *image_file;
static uint64_t allocated;
static int fail_next;
void pmem_get_stats(pmem_stats *s) { *s=(pmem_stats){0}; }
uint64_t pmem_largest_free_pages(void) { return 2048; }
void terminal_print_inline_hex64(uint64_t v) { printf("%llx",(unsigned long long)v); }
void *pmem_alloc_pages(uint64_t pages) {
    if (pages>2048 || fail_next) { fail_next=0;return 0; } /* fragmented RAM, max allocation 8 MiB */
    void *p=calloc((size_t)pages,4096);if (p) allocated+=pages;return p;
}
void *pmem_alloc_pages_lowdma(uint64_t pages) { return pmem_alloc_pages(pages); }
int kimg_load_svg_memory(kimg *out,const void *p,uint32_t n) { (void)out;(void)p;(void)n;return -1; }
int kimg_load_webp_memory(kimg *out,const void *p,uint32_t n) { (void)out;(void)p;(void)n;return -1; }
void pmem_free_pages(void *p,uint64_t pages) { assert(p && allocated>=pages);allocated-=pages;free(p); }
void usbh_dbg_dot(int n,unsigned int color) { (void)n;(void)color; }
volatile uint32_t g_disk_last_lba_lo,g_disk_last_count;
volatile int g_disk_last_rc;
void terminal_warn(const char *s) { puts(s); }
void terminal_boot_progress(const char *s) { (void)s; }
void terminal_print(const char *s) { puts(s); }
void terminal_print_inline(const char *s) { fputs(s,stdout); }
void terminal_print_inline_hex32(uint32_t v) { printf("%x",v); }
int kfile_open(KFile *f,const char *path,uint32_t flags) { (void)f;(void)flags;image_file=fopen(path,"rb");return image_file ? 0 : -1; }
uint64_t kfile_size(KFile *f) { (void)f;long pos=ftell(image_file);fseek(image_file,0,SEEK_END);long n=ftell(image_file);fseek(image_file,pos,SEEK_SET);return n; }
int kfile_read(KFile *f,void *out,uint32_t n,uint32_t *got) { (void)f;*got=(uint32_t)fread(out,1,n,image_file);return ferror(image_file) ? -1 : 0; }
int kfile_seek(KFile *f,uint64_t n) { (void)f;return fseek(image_file,(long)n,SEEK_SET); }
void kfile_close(KFile *f) { (void)f;fclose(image_file);image_file=0; }
int kfile_last_result(void) { return 0; }
int main(int argc,char **argv)
{
    if (argc!=2) return 2;
    assert(!kimg_prepare_decoder() && allocated==0);
    for (uint32_t i=0;i<4;++i) {
        kimg img={0};int rc=kimg_load(&img,argv[1]);
        printf("wallpaper decoder: rc=%d width=%u height=%u\n",rc,img.w,img.h);
        if (rc) return 1;
        pmem_free_pages(img.px,((uint64_t)img.w*img.h*4+4095)/4096);
        assert(allocated==0);
    }
    fail_next=1;assert(kimg_stbi_malloc(4096)==0 && allocated==0);
    void *p=kimg_stbi_malloc(4096);assert(p);kimg_stbi_arena_reset();assert(!allocated);
    puts("PASS: repeated wallpaper decoding without a contiguous 16 MiB arena, allocation failure/recovery, no scratch leaks");
    return 0;
}
