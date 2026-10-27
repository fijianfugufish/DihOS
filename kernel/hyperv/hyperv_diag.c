#include "hyperv/hyperv_diag.h"
#include "hyperv/hyperv_input.h"
#include "hyperv/hyperv_video.h"
#include "hyperv/hyperv_net.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/kfile.h"
#include "net/knet_usb.h"
#include "system/dihos_time.h"
#include "terminal/terminal_api.h"
#include "memory/pmem.h"
typedef struct trace_event { const char *name; uint64_t time; int32_t values[4]; } trace_event;
static trace_event events[256];
static uint32_t trace_on, next, count;
void hyperv_trace_enable(int enabled) { trace_on=enabled != 0; next=count=0; }
void hyperv_trace(const char *event, int32_t a, int32_t b, int32_t c, int32_t d)
{
    if (!trace_on) return;
    events[next]=(trace_event){event,dihos_time_monotonic_ms(),{a,b,c,d}};
    next=(next+1)%256; if (count<256) ++count;
}
static const char *timing_names[]={"network/input", "windows/apps", "terminal", "render", "present/runtime"};
static uint64_t timing_max[5], timing_last[5];
static uint32_t timing_slow[5];
void hyperv_timing_record(uint32_t stage, uint64_t started_ms)
{
    if (stage>=5) return;
    uint64_t elapsed=dihos_time_monotonic_ms()-started_ms;
    timing_last[stage]=elapsed;
    if (elapsed>timing_max[stage]) timing_max[stage]=elapsed;
    if (elapsed>=100) ++timing_slow[stage];
}
void hyperv_diag_print(void)
{
    KFile f; char text[256]; uint32_t n=0;
    terminal_print("Hyper-V diagnostic report (numeric values in hexadecimal)");
    if (!kfile_open(&f,"0:/OS/System/BUILD.TXT",KFILE_READ)) {
        if (!kfile_read(&f,text,sizeof(text)-1,&n)) { text[n]=0; terminal_print(text); }
        kfile_close(&f);
    } else terminal_warn("Build manifest marker unavailable; image may predate packaging");
    static const char *assets[]={
        "0:/OS/System/Images/bgpaper.jpg", "0:/OS/System/Images/wallpaper.jpg",
        "0:/OS/System/Images/bgpaper.bmp", "0:/OS/System/Images/Mouse/arrow.cur",
        "0:/OS/System/Fonts/Solarize.psf"};
    pmem_stats memory; pmem_get_stats(&memory);
    terminal_print_inline("memory total/free ordinary/largest run/free executable bytes: ");
    terminal_print_inline_hex64(memory.total_pages*4096);
    terminal_print_inline_hex64((memory.free_lowdma_pages+memory.free_high_pages)*4096);
    terminal_print_inline_hex64(pmem_largest_free_pages()*4096);
    terminal_print_inline_hex64(memory.free_executable_pages*4096); terminal_print("");
    terminal_print("Asset probes (path, size or filesystem result):");
    for (uint32_t i=0;i<sizeof(assets)/sizeof(assets[0]);++i) {
        terminal_print_inline(assets[i]); terminal_print_inline(" ");
        if (!kfile_open(&f,assets[i],KFILE_READ)) {
            terminal_print_hex64(kfile_size(&f)); kfile_close(&f);
        } else terminal_print_hex32((uint32_t)kfile_last_result());
    }
    const kfb *fb=kgfx_info();
    if (fb) {
        terminal_print_inline("display width/height/pitch/format: ");
        terminal_print_inline_hex32(fb->width); terminal_print_inline_hex32(fb->height);
        terminal_print_inline_hex32(fb->pitch); terminal_print_inline_hex32(fb->fmt); terminal_print("");
    }
    terminal_print(hyperv_video_online() ? "backend: synthvid + CPU" : "backend: GOP");
    terminal_print_inline("last CPU render microseconds: "); terminal_print_hex64(kgfx_last_render_us());
    terminal_print_inline("storage writable: "); terminal_print_hex32(kfile_storage_writable());
    terminal_print_inline("keyboard/mouse online: ");
    terminal_print_inline_hex32(hyperv_input_keyboard_online()); terminal_print_hex32(hyperv_input_mouse_online());
    uint64_t utc_seconds=0;
    terminal_print_inline("UTC Unix seconds (zero if unavailable): ");
    (void)dihos_time_utc(&utc_seconds);terminal_print_hex64(utc_seconds);
    terminal_print("UI timings: stage / last ms / maximum ms / pauses >=100ms");
    for (uint32_t i=0;i<5;++i) {
        terminal_print_inline(timing_names[i]);terminal_print_inline(" ");
        terminal_print_inline_hex64(timing_last[i]);terminal_print_inline(" ");
        terminal_print_inline_hex64(timing_max[i]);terminal_print_inline(" ");
        terminal_print_hex32(timing_slow[i]);
    }
    hyperv_net_diag_print();
    knet_usb_status net; knet_usb_get_status(&net);
    terminal_print(net.detail); terminal_print_inline("IPv4/mask/router/DNS: ");
    terminal_print_inline_hex32(net.ip); terminal_print_inline_hex32(net.mask);
    terminal_print_inline_hex32(net.router); terminal_print_hex32(net.dns);
    terminal_print("Recent input trace: time, event, a/b/c/d");
    for (uint32_t i=0;i<count;++i) {
        trace_event *e=&events[(next+256-count+i)%256];
        terminal_print_inline_hex64(e->time); terminal_print_inline(" "); terminal_print_inline(e->name);
        for (uint32_t j=0;j<4;++j) { terminal_print_inline(" "); terminal_print_inline_hex32((uint32_t)e->values[j]); }
        terminal_print("");
    }
}
