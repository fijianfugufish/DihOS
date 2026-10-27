#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define KGFX_COUNTER_OVERRIDE 1
static uint64_t kgfx_counter_now(void) { static uint64_t t; return ++t; }
static uint64_t kgfx_counter_frequency(void) { return 1000000; }
#include "../kernel/kwrappers/kgfx.c"

static uint64_t flushed_bytes, allocations, text_draws;
void *pmem_alloc_pages(uint64_t pages) { ++allocations;void *p=calloc((size_t)pages,4096);assert(p);return p; }
void pmem_free_pages(void *p,uint64_t pages) { (void)pages;free(p); }
void asm_dma_clean_range_raw(const void *p,uint64_t n) { (void)p;flushed_bytes+=n; }
void asm_dma_clean_range(const void *p,uint64_t n) { (void)p;(void)n; }
void asm_mmio_barrier(void) {}
void smp_get_snapshot(smp_snapshot *s) { memset(s,0,sizeof(*s)); }
int gpu_render_surface_create(const gpu_render_surface_desc *d,gpu_render_surface_handle *h) { (void)d;(void)h;assert(0);return -1; }
int gpu_render_surface_target(gpu_render_surface_handle h,gpu_render_target *t) { (void)h;(void)t;assert(0);return -1; }
int gpu_render_surface_texture(gpu_render_surface_handle h,gpu_render_sample_mode m,gpu_render_texture *t) { (void)h;(void)m;(void)t;assert(0);return -1; }
int gpu_render_surface_release(gpu_render_surface_handle h) { (void)h;return 0; }
uint32_t ktext_measure_line_px(const kfont *f,const char *s,uint32_t scale,int spacing) { (void)f;(void)spacing;return (uint32_t)strlen(s)*6*scale; }
uint32_t ktext_scale_mul_px(uint32_t p,uint32_t scale) { return p*scale; }
uint32_t ktext_line_height(const kfont *f,uint32_t scale,int spacing) { (void)f;return 8*scale+spacing; }
void ktext_draw_str_align_outline(const kfont *f,int x,int y,const char *s,kcolor c,uint8_t a,uint32_t scale,int cs,int ls,ktext_align align,uint32_t ow,kcolor oc,uint8_t oa)
{ (void)f;(void)x;(void)y;(void)s;(void)c;(void)a;(void)scale;(void)cs;(void)ls;(void)align;(void)ow;(void)oc;(void)oa;++text_draws;assert(G_argb_target);G_argb_target[0]=0xff000000u|(uint8_t)s[0]; }

static void run_resolution(uint32_t w,uint32_t h)
{
    uint32_t *screen=calloc((size_t)w*h,4);assert(screen);
    boot_info bi={0};bi.fb=(boot_fb){.fb_base=(uintptr_t)screen,.width=w,.height=h,.pitch=w*4,.pixel_format=1};
    assert(!kgfx_init(&bi));assert(!kgfx_scene_init());kgfx_set_coherent_cpu_present(1);
    uint32_t pixels[16*16];for (uint32_t i=0;i<256;++i) pixels[i]=0xff203040u+(i%16)*0x10000u;
    kgfx_obj_handle image=kgfx_obj_add_image(pixels,16,16,0,0,16);
    kgfx_obj *wallpaper=kgfx_obj_ref(image);wallpaper->u.image.w=w;wallpaper->u.image.h=h;
    kgfx_image_set_sample_mode(image,KGFX_IMAGE_SAMPLE_BILINEAR);
    kgfx_obj_handle cursor=kgfx_obj_add_rect(10,10,8,8,100,white,1);
    kgfx_window_layers_disable();kgfx_render_all(black);
    assert(g_rendered_frame_count==1 && g_have_prev_frame && flushed_bytes>=(uint64_t)w*h*4);
    assert(g_image_cache[image.idx].valid);
    uint32_t expected=g_image_cache[image.idx].pixels[10*w+10];
    uint32_t *cached=g_image_cache[image.idx].pixels;
    flushed_bytes=0;
    for (uint32_t i=0;i<20;++i) { kgfx_window_layers_disable();kgfx_render_all(black); }
    assert(g_rendered_frame_count==1 && flushed_bytes==0); /* no full repaint each frame */
    kgfx_obj *pointer=kgfx_obj_ref(cursor);pointer->u.rect.x=500;pointer->u.rect.y=300;
    kgfx_window_layers_disable();kgfx_render_all(black);
    assert(g_rendered_frame_count==2 && flushed_bytes>0 && flushed_bytes<65536);
    assert(screen[10*w+10]==expected && (screen[300*w+500]&0xffffff)==0xffffff);
    assert(g_image_cache[image.idx].pixels==cached); /* wallpaper is not rescaled */
    gpu_render_rect damage[24];assert(kgfx_cpu_present_damage(damage,24)>=2);
    /* An actual transition away from GPU layers still requires one full redraw. */
    kgfx_window_layers_prepare(0,0,1);kgfx_window_layers_disable();assert(!g_have_prev_frame);
    flushed_bytes=0;kgfx_render_all(black);assert(flushed_bytes>=(uint64_t)w*h*4);
    flushed_bytes=0;kgfx_window_layers_disable();kgfx_render_all(black);assert(!flushed_bytes);
    kgfx_image_cache_invalidate_all();kgfx_text_cache_invalidate_all();free(BB_ptr);BB_ptr=0;free(screen);
}
static void test_text_cache_reuse(void)
{
    kfont font={0};font.h=8;
    kgfx_obj object={0};object.kind=KGFX_OBJ_TEXT;object.alpha=255;
    object.u.text.font=&font;object.u.text.scale=1;object.fill=white;
    char line[64]="scroll 0000";object.u.text.text=line;
    uint64_t before=allocations;
    kgfx_text_cache *cache=kgfx_text_cache_ensure(0,&object);
    assert(cache && cache->valid && allocations==before+1);
    uint32_t *storage=cache->pixels;
    for (uint32_t i=0;i<2000;++i) {
        snprintf(line,sizeof(line),"scroll %04u",i);
        object.u.text.x=(int32_t)(i%400);object.u.text.y=(int32_t)(i%200);
        cache=kgfx_text_cache_ensure(0,&object);
        assert(cache && cache->valid && cache->pixels==storage && allocations==before+1);
        assert(cache->pixels[0]==(0xff000000u|'s'));
    }
    uint64_t draws=text_draws;object.u.text.x+=100;
    assert(kgfx_text_cache_ensure(0,&object)==cache && text_draws==draws);
    object.u.text.scale=4;cache=kgfx_text_cache_ensure(0,&object);
    assert(cache && cache->valid && allocations==before+2);
    kgfx_text_cache_invalidate_all();
}
int main(void)
{
    run_resolution(1920,1080);flushed_bytes=0;run_resolution(640,480);
    test_text_cache_reuse();
    puts("PASS: 2000 text-cache mutations reuse allocation; position-only change reuses raster; cache growth; production CPU renderer at 1080p/640x480: idle frames do no repaint, cursor updates stay below 64 KiB, old pixels restore, wallpaper cache persists, GPU-to-CPU transition redraws once");
    return 0;
}
