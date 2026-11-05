/* Bare-metal ARM64 integration test. Uses production page-table, timer and
 * exception/context code, not a simulated scheduler on the host. */
#include "asm/aa64_user_slice.h"
#include "memory/pmem.h"
#include "process/dihos_user_runtime.h"
#include "process/dihos_user_elf.h"
#include "process/dihos_user_memory.h"
#include "process/dihos_user_window.h"
#include "process/dihos_browser_bridge.h"
#include "../sdk/sacx/include/sacx_user_syscall.h"
#include "system/cpu_info.h"
#include "system/entropy.h"
#include <stddef.h>
#include "user_program.inc"
#ifdef USER_HAS_OPENSSL
/* Explicit test-owned entropy states, never a hardware-quality claim. */
int dihos_entropy_test_hardware(void){return 0;}
int dihos_entropy_test_word(uint64_t* word){(void)word;return -11;}
#endif
static uint64_t boot_root[512] __attribute__((aligned(4096)));
static uint64_t boot_l1[512] __attribute__((aligned(4096)));
static uint64_t boot_upper_l1[512] __attribute__((aligned(4096)));
static uint64_t *linear_root;
static uint64_t boot_ram_l2[512] __attribute__((aligned(4096)));
static uint64_t boot_ram_descriptor;
#ifndef USER_TEST_ARENA_PAGES
#define USER_TEST_ARENA_PAGES 32768u
#endif
static uint8_t arena[USER_TEST_ARENA_PAGES][4096] __attribute__((aligned(4096)));
static uint8_t occupied[USER_TEST_ARENA_PAGES];
static unsigned allocations;
static int fail_after=-1;
/* The emulated kernel/arena/GIC are all below 4 GiB. Host pmem tests exercise
 * the real firmware-map exclusion policy; this harness owns no UEFI map. */
int pmem_user_arena_available(uint64_t base,uint64_t bytes){
    return base>=(64ull<<30) && bytes && base<=(512ull<<30)-bytes;
}
void *memcpy(void *out,const void *in,size_t n){for(size_t i=0;i<n;++i)((uint8_t*)out)[i]=((const uint8_t*)in)[i];return out;}
void *memset(void *out,int v,size_t n){for(size_t i=0;i<n;++i)((uint8_t*)out)[i]=(uint8_t)v;return out;}
int memcmp(const void* a,const void* b,size_t n){for(size_t i=0;i<n;++i){int d=((const uint8_t*)a)[i]-((const uint8_t*)b)[i];if(d)return d;}return 0;}
static void print(const char *text){volatile uint32_t *uart=(volatile uint32_t*)0x09000000;while(*text){while(uart[6]&32){}uart[0]=(uint8_t)*text++;}}
static int text_prefix(const char* text,const char* prefix){while(*prefix){if(*text++!=*prefix++)return 0;}return 1;}
static int text_contains(const char* text,const char* part){for(;*text;++text)if(text_prefix(text,part))return 1;return 0;}
static void hex(uint64_t v){char out[19]="0x";const char *d="0123456789abcdef";for(unsigned i=0;i<16;++i)out[i+2]=d[(v>>((15-i)*4))&15];out[18]=0;print(out);}
static void finish(int status){uint64_t p[2]={0x20026,(uint64_t)status};register uint64_t x0 __asm__("x0")=0x20,x1 __asm__("x1")=(uintptr_t)p;__asm__ volatile("hlt #0xf000"::"r"(x0),"r"(x1):"memory");for(;;){}}
static void require(int ok,const char *label){if(!ok){print("FAIL: ");print(label);print("\n");finish(1);}}
void aa64_exception_common(void){uint64_t pc,esr,far;__asm__ volatile("mrs %0,elr_el1; mrs %1,esr_el1; mrs %2,far_el1":"=r"(pc),"=r"(esr),"=r"(far));print("FATAL kernel PC ");hex(pc);print(" ESR ");hex(esr);print(" FAR ");hex(far);print("\n");finish(1);}
void terminal_print(const char *s){print(s);print("\n");}
void terminal_print_inline_hex64(uint64_t n){hex(n);}
void terminal_flush_log(void){}
void *pmem_alloc_pages(uint64_t n){if(!n||n>USER_TEST_ARENA_PAGES||fail_after==0)return 0;if(fail_after>0)--fail_after;for(unsigned i=0;i+n<=USER_TEST_ARENA_PAGES;++i){unsigned j=0;for(;j<n&&!occupied[i+j];++j){}if(j!=n){i+=j;continue;}for(j=0;j<n;++j)occupied[i+j]=1;allocations+=(unsigned)n;return arena[i];}return 0;}
void pmem_free_pages(void *p,uint64_t n){unsigned index=(unsigned)(((uintptr_t)p-(uintptr_t)arena)/4096);require(index+n<=USER_TEST_ARENA_PAGES,"free bounds");for(unsigned j=0;j<n;++j){require(occupied[index+j],"double free");occupied[index+j]=0;}allocations-=(unsigned)n;}
#ifdef USER_HAS_HTTP
#include "apps/sacx_network_internal.h"
static sacx_net_request_slot *G_net_active_slot;
#include "../kernel/apps/sacx_user_network.inc"
#ifdef USER_NATIVE_PAGE_NAVIGATION
static unsigned nav_active,nav_redirects,nav_styles,nav_scripts,nav_next,nav_clicked,nav_green,nav_stalled,nav_typed,nav_form,nav_failures;
#ifdef USER_BLOB_TESTS
#include "browser_fixtures/blob_probe.inc"
#define FRAME_BLOB_SCRIPT "addEventListener('message',async function(e){if(e.origin==='https://example.com'&&e.data&&e.data.blobProbe){try{await fetch(e.data.blobProbe);parent.postMessage('blob-leak','*')}catch(error){parent.postMessage('blob-denied','https://example.com')}}});"
#else
#define BLOB_TEST_HTML ""
#define FRAME_BLOB_SCRIPT ""
#endif
#ifdef USER_FAILED_IMAGE_TESTS
#include "browser_fixtures/failed_images.inc"
#else
#define FAILED_IMAGE_TEST_HTML ""
#endif
#ifdef USER_CSS_ALLOCATION_TESTS
#include "browser_fixtures/css_allocation.inc"
#else
#define CSS_ALLOCATION_TEST_HTML ""
#endif
#ifdef USER_FRAME_TESTS
#ifdef USER_COOKIE_TESTS
#define FRAME_COOKIE_SCRIPT "if(location.hostname==='captcha.example'){window.cookieState=typeof window.cookieStore==='undefined'&&navigator.cookieEnabled&&document.cookie.indexOf('visible=server')>=0&&document.cookie.indexOf('http_only=')<0;document.cookie='js=one; Path=/; Secure; SameSite=None';document.cookie='http_only=changed; Path=/; Secure; SameSite=None';document.cookie='bad=leak; Domain=example.com; Path=/; Secure; SameSite=None';window.cookieState=window.cookieState&&document.cookie.indexOf('js=one')>=0&&document.cookie.indexOf('http_only=')<0&&document.cookie.indexOf('bad=')<0}"
#else
#define FRAME_COOKIE_SCRIPT ""
#endif
#ifdef USER_POST_TESTS
#define FRAME_BUTTON_SCRIPT "this.style.background='#24ac68';var x=new XMLHttpRequest;x.open('POST','/post-target');x.onload=function(){if(x.responseText==='ack'&&x.getResponseHeader('Set-Cookie')===null&&x.getAllResponseHeaders().toLowerCase().indexOf('set-cookie')<0&&document.cookie.indexOf('after=')<0)parent.postMessage('clicked','*')};x.send(window.cookieState===false?'FAIL':'a=42')"
#else
#define FRAME_BUTTON_SCRIPT "this.style.background='#24ac68';parent.postMessage('clicked','*')"
#endif
#define FRAME_TEST_HTML "<iframe style='position:absolute;left:0;top:240px;width:180px;height:100px;border:0' src='/child-frame'></iframe><iframe style='position:absolute;left:200px;top:240px;width:180px;height:100px;border:0' src='https://captcha.example/child-frame'></iframe><iframe sandbox='allow-scripts' style='position:absolute;left:400px;top:240px;width:180px;height:100px;border:0' src='/sandbox-frame'></iframe><iframe style='position:absolute;left:0;top:350px;width:180px;height:60px;border:0' src='/blocked-frame'></iframe><div id='frame-result' style='position:absolute;left:600px;top:240px;width:20px;height:20px;background:#ff0000'></div><script>addEventListener('message',function(e){if(e.origin==='https://captcha.example'&&e.data==='clicked')document.getElementById('frame-result').style.background='#24ac68'});</script>"
#else
#define FRAME_TEST_HTML ""
#endif
#ifdef USER_IMAGE_TESTS
#include "browser_fixtures/images/fixtures.inc"
#define IMAGE_TEST_HTML "<div style='position:absolute;top:160px;left:0;display:flex'><svg width='40' height='40'><rect width='40' height='40' fill='#2060e0'/></svg><img width=40 height=40 src=\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='4' height='4'%3E%3Crect width='4' height='4' fill='%232060e0'/%3E%3C/svg%3E\"><img width=40 height=40 src='/image.svg'><img width=40 height=40 src='/image.png'><img width=40 height=40 src='/image.jpeg'><img width=40 height=40 src='/image.gif'><img width=40 height=40 src='/image.bmp'><img width=40 height=40 src='/image.webp'></div>"
#else
#define IMAGE_TEST_HTML ""
#endif
#ifdef USER_NATIVE_PAGE_HTTPS
#define NAVIGATION_ORIGIN "https://example.com/"
#else
#define NAVIGATION_ORIGIN "http://example.com/"
#endif
#endif
/* Deterministic transport model only. Real DNS/TCP/TLS is a separate guest
 * gate; these tests cross the actual EL0/SVC/VM-copy/broker boundary. */
#ifdef USER_CONSENT_TESTS
#define CONSENT_TEST_HTML "<div id=consent-probe style='position:absolute;left:600px;top:355px;width:16px;height:16px;background:red'></div><script>(async()=>{try{document.cookie='SOCS=test; Max-Age=3600; Path=/; Secure';let r=await fetch('/consent-upgrade',{method:'POST',mode:'same-origin',credentials:'same-origin',cache:'no-store',headers:{'Content-Type':'application/x-www-form-urlencoded'}});if(!r.ok)throw Error('upgrade');await fetch('https://consent.example/save',{method:'POST',mode:'cors',credentials:'include',cache:'no-store',headers:{'Content-Type':'text/plain'}});let t=await r.text();if(t.includes('state_switch'))document.getElementById('consent-probe').style.background='#24ac68'}catch(e){console.error('consent fixture: '+e)}})();</script>"
#else
#define CONSENT_TEST_HTML ""
#endif
static void user_http_transport_step(void){
    for(unsigned i=0;i<USER_HTTP_SLOTS;++i){
        sacx_net_request_slot *r=&G_user_http[i].request;
        if(!r->used)continue;
        if(r->status==SACX_NET_STATUS_QUEUED){r->status=SACX_NET_STATUS_LOADING;continue;}
        if(r->status==SACX_NET_STATUS_LOADING){
#ifdef USER_CONSENT_TESTS
            if(nav_active && (text_prefix(r->url,NAVIGATION_ORIGIN "consent-upgrade") || text_prefix(r->url,"https://consent.example/save"))){
                unsigned upgrade=text_prefix(r->url,NAVIGATION_ORIGIN "consent-upgrade");
                require(!memcmp(r->method,"POST",5) && !r->request_body_size,"bodyless fetch remains POST");
                if(upgrade)require(text_contains(r->request_headers,"SOCS=test"),"same-origin fetch receives DOM consent cookie");
                const char* head=upgrade?"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 21\r\n\r\n":"HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: https://example.com\r\nAccess-Control-Allow-Credentials: true\r\n\r\n";
                const char* body="{\"state_switch\":true}";unsigned n=0;while(head[n])++n;memcpy(r->stream.headers,head,n);r->stream.header_bytes=n;r->stream.headers_ready=1;
                n=0;if(upgrade){while(body[n])++n;memcpy(r->buffer,body,n);}r->body_size=n;r->info.http_status=upgrade?200:204;r->tls_verified=1;r->status=SACX_NET_STATUS_DONE;continue;
            }
#endif
#ifdef USER_IMAGE_TESTS
            if(nav_active && text_prefix(r->url,NAVIGATION_ORIGIN "image.")){
                const unsigned char* bytes=0;unsigned count=0;
#define IMAGE_REPLY(ext) if(text_prefix(r->url,NAVIGATION_ORIGIN "image." #ext)){bytes=image_##ext;count=sizeof(image_##ext);}
                IMAGE_REPLY(png) IMAGE_REPLY(jpeg) IMAGE_REPLY(gif) IMAGE_REPLY(bmp) IMAGE_REPLY(webp) IMAGE_REPLY(svg)
#undef IMAGE_REPLY
                require(bytes!=0,"known external image URL");
                const char* head=text_prefix(r->url,NAVIGATION_ORIGIN "image.svg")?"HTTP/1.1 200 OK\r\nContent-Type: image/svg+xml\r\n\r\n":"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n\r\n";
                unsigned n=0;while(head[n])++n;memcpy(r->stream.headers,head,n);r->stream.header_bytes=n;r->stream.headers_ready=1;
                memcpy(r->buffer,bytes,count);r->body_size=count;r->info.http_status=200;r->tls_verified=1;r->status=SACX_NET_STATUS_DONE;continue;
            }
#endif
#ifdef USER_NATIVE_PAGE_NAVIGATION
            const char* frame_body=0;
#ifdef USER_FRAME_TESTS
            if(nav_active && (text_prefix(r->url,"https://captcha.example/post-target") || text_prefix(r->url,NAVIGATION_ORIGIN "post-target"))){
                require(!memcmp(r->method,"POST",5) && r->request_body_size==4 && !memcmp(r->request_body,"a=42",4),"native iframe XMLHttpRequest carries its POST method and owned body");
#ifdef USER_COOKIE_TESTS
                require(text_contains(r->request_headers,"http_only=secret")&&text_contains(r->request_headers,"visible=server")&&text_contains(r->request_headers,"js=one")&&!text_contains(r->request_headers,"changed")&&!text_contains(r->request_headers,"bad="),"HTTP and DOM share scoped cookie storage, with protected HttpOnly state");
                print("PASS: iframe DOM and HTTP share session cookies; HttpOnly and cross-domain writes are denied\n");
#endif
                frame_body="ack";print("PASS: iframe JavaScript performs asynchronous POST through checked SACX operation 40\n");
            }
#endif
#ifdef USER_FRAME_TESTS
            if(nav_active && (text_prefix(r->url,NAVIGATION_ORIGIN "child-frame") || text_prefix(r->url,"https://captcha.example/child-frame")))
                frame_body="<!doctype html><style>body{margin:0}button{display:block;width:160px;height:80px;background:#2468ac;border:0}</style><button onclick=\"" FRAME_BUTTON_SCRIPT "\">Click child</button><script>" FRAME_COOKIE_SCRIPT FRAME_BLOB_SCRIPT "if(location.hostname==='captcha.example')try{parent.document.body.style.background='#ff0000'}catch(e){}</script>";
            if(nav_active && text_prefix(r->url,NAVIGATION_ORIGIN "sandbox-frame"))
                frame_body="<!doctype html><style>body{margin:0;background:#ff0000}</style><body><script>try{parent.document.body.style.background='#ff0000'}catch(e){document.body.style.background='#68ac24'}</script>";
            unsigned blocked_frame=nav_active && text_prefix(r->url,NAVIGATION_ORIGIN "blocked-frame");
            if(blocked_frame)frame_body="<!doctype html><style>body{margin:0;background:#ff00ff}</style><script>parent.document.body.style.background='#ff0000'</script>";
            if(frame_body){
                const char* head=blocked_frame?"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nX-Frame-Options: DENY\r\n\r\n":"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n";
#ifdef USER_COOKIE_TESTS
                if(text_prefix(r->url,"https://captcha.example/child-frame"))head="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nSet-Cookie: http_only=secret; Secure; HttpOnly; SameSite=None; Path=/\r\nSet-Cookie: visible=server; Secure; SameSite=None; Path=/\r\n\r\n";
                if(text_prefix(r->url,"https://captcha.example/post-target"))head="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nSet-Cookie: after=done; Secure; HttpOnly; SameSite=None; Path=/\r\n\r\n";
#endif
                unsigned n=0;while(frame_body[n])++n;memcpy(r->buffer,frame_body,n);r->body_size=n;n=0;while(head[n])++n;memcpy(r->stream.headers,head,n);r->stream.header_bytes=n;r->stream.headers_ready=1;r->info.http_status=200;r->tls_verified=1;r->status=SACX_NET_STATUS_DONE;continue;
            }
#endif
            if(nav_active && text_prefix(r->url,NAVIGATION_ORIGIN "stalled")){nav_stalled=1;continue;}
            if(nav_active && text_prefix(r->url,NAVIGATION_ORIGIN "fail-once") && !(nav_failures++&1)){
                const char error[]="Modeled DNS failure <test> & detail";
                memcpy(r->info.error,error,sizeof(error));r->status=SACX_NET_STATUS_FAILED;continue;
            }
#endif
            if(r->request_header_bytes){
#ifdef USER_NATIVE_PAGE_NAVIGATION
                if(!nav_active){
#endif
                const char retained[]="X-DihOS-Test: retained\r\n";
                require(r->no_follow && r->request_header_bytes>=sizeof(retained)-1 &&
                    text_prefix(r->request_headers,retained),"HTTP descriptor owns fields after EL0 input mutation/disposal");
#ifdef USER_NATIVE_PAGE_NAVIGATION
                }
#endif
            }
            static const char headers[]="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nContent-Length: 5\r\n\r\n";
            memcpy(r->stream.headers,headers,sizeof(headers)-1);r->stream.header_bytes=sizeof(headers)-1;r->stream.headers_ready=1;
            memcpy(r->buffer,"hello",5);r->body_size=5;r->info.http_status=200;r->status=SACX_NET_STATUS_DONE;
            // Modeled successful TLS outcome only. Real chain/hostname/time
            // rejection is covered by the native BearSSL certificate tests.
            if(text_prefix(r->url,"https://"))r->tls_verified=1;
#ifdef USER_NATIVE_PAGE_NAVIGATION
            if(nav_active && text_prefix(r->url,NAVIGATION_ORIGIN)){
                const char *body=0,*type="text/html";
                if(text_prefix(r->url,NAVIGATION_ORIGIN "preview"))body="<!doctype html><meta charset=utf-8><link rel=stylesheet href='/style.css'><h1 id=title>loading</h1><a id=box href='/next'>Open next page</a><script src='/app.js'></script>" IMAGE_TEST_HTML FRAME_TEST_HTML BLOB_TEST_HTML FAILED_IMAGE_TEST_HTML CSS_ALLOCATION_TEST_HTML CONSENT_TEST_HTML;
                else if(text_prefix(r->url,NAVIGATION_ORIGIN "failed-image")){r->status=SACX_NET_STATUS_FAILED;continue;}
                else if(text_prefix(r->url,NAVIGATION_ORIGIN "style.css")){++nav_styles;type="text/css";body="html,body{margin:0;background:white;font-family:sans-serif}h1{margin:0;font-size:24px;line-height:32px}#box{display:block;width:192px;height:64px;background:#2468ac;transform:translate3d(0,0,0);will-change:transform}";}
                else if(text_prefix(r->url,NAVIGATION_ORIGIN "app.js")){++nav_scripts;type="text/javascript";body="document.getElementById('title').textContent='fetched DOM + JS';document.getElementById('box').style.width='224px';";}
                else if(text_prefix(r->url,NAVIGATION_ORIGIN "next")){++nav_next;body="<!doctype html><meta charset=utf-8><style>html,body{margin:0;background:white}h1{margin:0;font-size:24px;line-height:32px}#box{display:block;width:224px;height:64px;background:#24ac68}</style><h1 id=title>followed link</h1><a id=box href='/stalled'>Wait for loading</a>";}
                else if(text_prefix(r->url,NAVIGATION_ORIGIN "typed")){++nav_typed;body="<!doctype html><meta charset=utf-8><style>html,body{margin:0;background:white}h1{margin:0;font-size:24px;line-height:32px}#box{display:block;width:224px;height:64px;background:#ac2468}</style><h1>typed address</h1><a id=box href='/stalled'>Stall</a><input id=field oninput=\"if(this.value==='k')document.getElementById('box').style.background='#2468ac'\"><script>document.addEventListener('keydown',function(e){if(e.key==='k')document.getElementById('box').style.background='#ac6824';if(e.key==='l'){e.preventDefault();setTimeout(function(){document.getElementById('box').style.background=document.getElementById('field').value==='k'?'#68ac24':'#ff0000'},0)}});</script>";}
                else if(text_prefix(r->url,NAVIGATION_ORIGIN "viewport")){body="<!doctype html><meta charset=utf-8><style>html,body{margin:0}#top{height:500px;background:#123456}@media(min-width:1000px){#top{background:#654321}}#lower{height:1500px;background:#456789}#fixed{position:fixed;top:0;left:0;width:20px;height:20px;background:#cdab12}</style><div id=top></div><div id=lower></div><div id=fixed></div><script>addEventListener('resize',function(){if(innerWidth>=1000)document.getElementById('fixed').style.background='#ab12cd'});</script>";}
                else if(text_prefix(r->url,NAVIGATION_ORIGIN "fail-once"))body="<!doctype html><style>html,body{margin:0;background:#19aa77}</style><title>Recovered</title>Retry succeeded";
                if(body){
                    unsigned length=0;while(body[length])++length;memcpy(r->buffer,body,length);r->body_size=length;
                    const char *head=type[5]=='c'?"HTTP/1.1 200 OK\r\nContent-Type: text/css\r\n\r\n":type[5]=='j'?"HTTP/1.1 200 OK\r\nContent-Type: text/javascript\r\n\r\n":"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n";
                    length=0;while(head[length])++length;memcpy(r->stream.headers,head,length);r->stream.header_bytes=length;
                }else{
                    ++nav_redirects;r->body_size=0;r->info.http_status=302;
                    const char redirect[]="HTTP/1.1 302 Found\r\nLocation: /preview\r\nContent-Type: text/html\r\n\r\n";
                    memcpy(r->stream.headers,redirect,sizeof(redirect)-1);r->stream.header_bytes=sizeof(redirect)-1;
                }
            }
#endif
        }
    }
}
#endif
uint64_t pmem_virt_to_phys(const void *p){return(uintptr_t)p;}
static unsigned published_pages;
void asm_dma_clean_range(const void *p,uint64_t bytes){
    uintptr_t at=(uintptr_t)p&~63ull;for(;at<(uintptr_t)p+bytes;at+=64)__asm__ volatile("dc cvac,%0"::"r"(at):"memory");
    __asm__ volatile("dsb sy; isb":::"memory");++published_pages;
}
void *pmem_phys_to_virt(uint64_t p){return(void*)(uintptr_t)p;}
int aarch64_mmu_map_device_identity(uint64_t p,uint64_t n){return (p>=0x08000000&&p+n<=0x09000000)?0:-1;}
#ifndef USER_TEST_GICR_BASE
#define USER_TEST_GICR_BASE 0x080a0000ull
#endif
/* Model a page-offset ACPI register window over QEMU's fixed GICR device.
 * Translation exists only in the test MMIO adapter, never in the driver. */
static uint64_t mmio_address(uint64_t p){return p>=USER_TEST_GICR_BASE && p<USER_TEST_GICR_BASE+0x20000 ? 0x080a0000+(p-USER_TEST_GICR_BASE):p;}
int asm_aa64_try_read32(uint64_t p,uint32_t *v){*v=*(volatile uint32_t*)(uintptr_t)mmio_address(p);return 0;}
int asm_aa64_try_write32(uint64_t p,uint32_t v){*(volatile uint32_t*)(uintptr_t)mmio_address(p)=v;__asm__ volatile("dsb sy":::"memory");return 0;}
void asm_sync_executable_range(const void *p,uint64_t n){uintptr_t a=(uintptr_t)p&~63ull;for(;a<(uintptr_t)p+n;a+=64)__asm__ volatile("dc cvau,%0"::"r"(a):"memory");__asm__ volatile("dsb ish; ic iallu; dsb ish; isb":::"memory");}
static uint64_t simulated_frame_ms;
uint64_t dihos_time_monotonic_ms(void){uint64_t ticks,freq;__asm__ volatile("mrs %0,cntvct_el0; mrs %1,cntfrq_el0":"=r"(ticks),"=r"(freq));return ticks/(freq/1000)+simulated_frame_ms;}
#include "../kernel/system/dihos_wallclock.inc"
#ifndef USER_TEST_TIMER_FLAGS
#define USER_TEST_TIMER_FLAGS 0u
#endif
void cpu_info_get(cpu_info_snapshot *out){*out=(cpu_info_snapshot){0};out->initialized=1;out->gic_version=3;out->gic_distributor=0x08000000;out->gic_redistributor=USER_TEST_GICR_BASE;out->gic_redistributor_bytes=0x20000;out->virtual_timer_irq=27;out->virtual_timer_flags=USER_TEST_TIMER_FLAGS;}
static void memory_init(void){
    linear_root=boot_root;
#ifdef USER_TEST_FIRMWARE_L0
    linear_root=boot_l1;boot_root[0]=(uintptr_t)boot_l1|3;
#endif
    linear_root[0]=1ull|(1ull<<10)|(1ull<<2)|(1ull<<53)|(1ull<<54);
    linear_root[1]=0x40000000ull|1ull|(1ull<<10)|(3ull<<8)|(1ull<<54);
#ifdef USER_TEST_FIRMWARE_L2
    for(unsigned i=0;i<512;++i)boot_ram_l2[i]=(0x40000000ull+(uint64_t)i*0x200000)|1ull|(1ull<<10)|(3ull<<8)|(1ull<<52)|(1ull<<54);
    linear_root[1]=(uintptr_t)boot_ram_l2|3;
#endif
    boot_ram_descriptor=linear_root[1];
    uint64_t mair=0x04ff,tcr=25ull|(1ull<<8)|(1ull<<10)|(3ull<<12)|(1ull<<23),sctlr;
#ifdef USER_TEST_FIRMWARE_L0
    tcr=(tcr&~63ull)|24;
#endif
    __asm__ volatile("dsb sy; msr mair_el1,%1; msr tcr_el1,%2; msr ttbr0_el1,%3; isb; mrs %0,sctlr_el1":"=r"(sctlr):"r"(mair),"r"(tcr),"r"((uintptr_t)boot_root):"memory");
    sctlr=(sctlr|1|(1<<2)|(1<<12))&~(1ull<<19);
    __asm__ volatile("msr sctlr_el1,%0; isb"::"r"(sctlr):"memory");
}
static void install(uint32_t *code,const uint32_t *program,unsigned words,aa64_user_cpu *cpu,const aarch64_user_vm *vm){
    memcpy(code,program,words*4);asm_sync_executable_range(code,words*4);memset(cpu,0,sizeof(*cpu));cpu->root_phys=vm->root_phys;cpu->frame.fault.elr=0x100000000ull;cpu->frame.sp=0x100011000ull;
}
static int run_to_trap(aa64_user_cpu *cpu){
    int event=AA64_USER_PREEMPTED;
    for(unsigned n=0;n<100 && event==AA64_USER_PREEMPTED;++n)event=aa64_user_slice_run(cpu,1000);
    return event;
}
static __attribute__((noinline)) void sparse_kernel_tests(void){
    static uint64_t sparse_firmware_l2[16][512] __attribute__((aligned(4096)));
    uint64_t original[16];
    for(unsigned i=0;i<16;++i)for(unsigned j=0;j<512;++j)
        sparse_firmware_l2[i][j]=((uint64_t)(64+i)<<30)+(uint64_t)j*2097152+1ull+(1ull<<10)+(3ull<<8)+(1ull<<52)+(1ull<<54);
    asm_dma_clean_range(sparse_firmware_l2,sizeof(sparse_firmware_l2));
    for(unsigned i=0;i<16;++i){original[i]=linear_root[64+i];linear_root[64+i]=(uintptr_t)sparse_firmware_l2[i]|3;}
    asm_dma_clean_range(linear_root+64,16*8);
    __asm__ volatile("dsb ishst; tlbi vmalle1is; dsb ish; isb":::"memory");
    aarch64_user_vm vm;dihos_user_memory memory={0};memory.limit=1024*1024;
    require(!aarch64_user_vm_create(&vm),"sparse failure VM clone");
    uint64_t args[3]={1ull<<30,1ull<<30,1};
    int64_t address=dihos_user_memory_call(&memory,&vm,SACX_USER_SPARSE_RESERVE,args);
    require(address>0 && !memory.charged && !memory.committed,"GiB reserve owns no payload pages");
    unsigned initial_pages=allocations,initial_tables=vm.owned_table_count;
    args[0]=address;args[1]=65536;args[2]=3;
    for(unsigned failure=0;failure<19;++failure){
        fail_after=(int)failure;
        int64_t rc=dihos_user_memory_call(&memory,&vm,SACX_USER_MEMORY_COMMIT,args);fail_after=-1;
        require(rc==-12 && allocations==initial_pages && vm.owned_table_count==initial_tables &&
            !memory.charged && !memory.committed,"sparse metadata/payload/page-table OOM rollback");
        require(aarch64_user_vm_check_buffer(&vm,address,4096,0)==-14,"failed sparse commit grants no access");
    }
    memory.limit=65536;
    require(dihos_user_memory_call(&memory,&vm,SACX_USER_MEMORY_COMMIT,args)==-12 && allocations==initial_pages,
            "sparse physical quota includes metadata");memory.limit=1024*1024;
    void *blockers[64];
    for(unsigned i=0;i<64;++i){blockers[i]=pmem_alloc_pages(1);require(blockers[i]!=0,"fragmented backing setup");}
    for(unsigned i=0;i<64;i+=2)pmem_free_pages(blockers[i],1);
    require(!dihos_user_memory_call(&memory,&vm,SACX_USER_MEMORY_COMMIT,args),"fragmented sparse commit");
    uint64_t physical[2];
    require(!aarch64_user_vm_translate(&vm,address,&physical[0]) &&
            !aarch64_user_vm_translate(&vm,address+4096,&physical[1]) && physical[1]!=physical[0]+4096,
            "sparse backing is actually noncontiguous");
    physical[1]=0;
    require(aarch64_user_vm_protect_scatter(&vm,address,physical,2,1)==-14 &&
            !aarch64_user_vm_check_buffer(&vm,address,4096,1),"scatter update validates whole range before changing permissions");
    uint64_t kernel_alias;
    require(!aarch64_user_vm_translate_current(address,&kernel_alias) && kernel_alias==(uint64_t)address,
            "private arena leaves firmware/kernel identity mapping intact");
    args[2]=0;require(!dihos_user_memory_call(&memory,&vm,SACX_USER_MEMORY_DECOMMIT,args) &&
            !memory.charged && !memory.committed && vm.owned_table_count==initial_tables,
            "decommit returns backing, metadata and empty page tables");
    for(unsigned i=1;i<64;i+=2)pmem_free_pages(blockers[i],1);
    args[1]=args[2]=0;
    require(!dihos_user_memory_call(&memory,&vm,SACX_USER_SPARSE_RELEASE,args),"release empty sparse region");
    uint64_t regions[16];args[0]=4096;args[1]=4096;args[2]=1;
    for(unsigned i=0;i<16;++i){int64_t v=dihos_user_memory_call(&memory,&vm,SACX_USER_SPARSE_RESERVE,args);require(v>0,"sparse region slots");regions[i]=(uint64_t)v;}
    require(dihos_user_memory_call(&memory,&vm,SACX_USER_SPARSE_RESERVE,args)==-12,"sparse slot exhaustion is bounded");
    for(unsigned i=0;i<16;++i){args[0]=regions[i];args[1]=args[2]=0;require(!dihos_user_memory_call(&memory,&vm,SACX_USER_SPARSE_RELEASE,args),"release sparse slot");}
    require(!memory.reserved && allocations==initial_pages,"sparse reservation/release needs no data backing");
    aarch64_user_vm peer;dihos_user_memory peer_memory={0};peer_memory.limit=1024*1024;
    require(!aarch64_user_vm_create(&peer),"second sparse VM clone");
    args[0]=4096;args[1]=4096;args[2]=1;
    int64_t own=dihos_user_memory_call(&memory,&vm,SACX_USER_SPARSE_RESERVE,args);
    int64_t other=dihos_user_memory_call(&peer_memory,&peer,SACX_USER_SPARSE_RESERVE,args);
    require(own>0 && own==other,"processes may use identical sparse virtual addresses");
    args[0]=own;args[1]=4096;args[2]=3;
    require(!dihos_user_memory_call(&memory,&vm,SACX_USER_MEMORY_COMMIT,args) &&
            aarch64_user_vm_check_buffer(&peer,other,1,0)==-14,"private commit cannot grant peer access");
    unsigned char own_value=42,other_value=73,observed=0;
    require(!aarch64_user_vm_copy_out(&vm,own,&own_value,1),"write first private sparse payload");
    require(!dihos_user_memory_call(&peer_memory,&peer,SACX_USER_MEMORY_COMMIT,args) &&
            !aarch64_user_vm_copy_in(&peer,&observed,other,1) && !observed,"peer sparse payload starts independently zeroed");
    require(!aarch64_user_vm_copy_out(&peer,other,&other_value,1) &&
            !aarch64_user_vm_copy_in(&vm,&observed,own,1) && observed==42,"same virtual address has isolated physical backing");
    args[1]=args[2]=0;
    require(!dihos_user_memory_call(&memory,&vm,SACX_USER_SPARSE_RELEASE,args) &&
            !dihos_user_memory_call(&peer_memory,&peer,SACX_USER_SPARSE_RELEASE,args),"release private sparse payloads");
    aarch64_user_vm_release(&peer);dihos_user_memory_destroy(&peer_memory);
    aarch64_user_vm_release(&vm);dihos_user_memory_destroy(&memory);require(!allocations,"sparse kernel test cleanup");
    // Force distinct L3 tables with very little physical payload. This must
    // not depend on a process exhausting its byte quota first.
    aarch64_user_vm wide;dihos_user_memory wide_memory={0};wide_memory.limit=16*1024*1024;
    require(!aarch64_user_vm_create(&wide),"wide metadata VM clone");
    args[0]=4ull*1024*1024*1024;args[1]=1ull<<30;args[2]=1;
    int64_t wide_base=dihos_user_memory_call(&wide_memory,&wide,SACX_USER_SPARSE_RESERVE,args);
    require(wide_base>0,"wide metadata reservation");
    unsigned mapped=0;int64_t wide_error=0;
    for(;mapped<256;++mapped){args[0]=(uint64_t)wide_base+(uint64_t)mapped*2097152;args[1]=4096;args[2]=3;
        wide_error=dihos_user_memory_call(&wide_memory,&wide,SACX_USER_MEMORY_COMMIT,args);if(wide_error)break;}
#ifdef USER_EXPECT_OLD_TABLE_LIMIT
    require(wide_error==-12 && mapped<256 && wide.owned_table_count==192 && wide_memory.charged<wide_memory.limit/2,
            "old metadata ceiling fails with physical quota remaining");
    print("PASS: 192-table control exhausts metadata before its byte quota\n");
#else
    require(mapped==256 && !wide_error && wide.owned_table_count>192 && wide.owned_table_count<=512 && wide_memory.charged<wide_memory.limit/2,
            "256 scattered pages exceed old table ceiling without exhausting physical quota");
    print("PASS: 256 scattered mappings exceed 192 tables with physical quota remaining\n");
#endif
    aarch64_user_vm_release(&wide);dihos_user_memory_destroy(&wide_memory);require(!allocations,"wide metadata cleanup");
    for(unsigned i=0;i<16;++i)linear_root[64+i]=original[i];
    asm_dma_clean_range(linear_root+64,16*8);
    __asm__ volatile("dsb ishst; tlbi vmalle1is; dsb ish; isb":::"memory");
    print("PASS: sparse quota/slot bounds, fragmented backing, 19 transactional OOM points, page-table reclaim, process isolation and unchanged kernel mapping\n");
}
#ifdef USER_NATIVE_PAGE_WINDOW
static unsigned window_frames,window_closed;
static uint64_t window_id;
static unsigned window_height,window_width;
static const uint32_t* last_window_pixels;
static unsigned region_presentations;
#ifdef USER_NATIVE_PAGE_EMBEDDED
static uint32_t browser_token,browser_stage;
static void browser_send(uint32_t type,int a,int b,int c,int d,const char* text){
 sacx_browser_send send={.session=browser_token,.command={.type=type,.a=a,.b=b,.c=c,.d=d}};
 if(text){unsigned i=0;while(text[i] && i+1<sizeof(send.command.text)){send.command.text[i]=text[i];++i;}}
 require(!dihos_browser_send(1,&send),"existing UI sends bounded browser command");
}
static void browser_click(int x,int y){browser_send(SACX_BROWSER_MOUSE,x,y,1,0,0);browser_send(SACX_BROWSER_MOUSE,x,y,0,0,0);}
static int pending_browser_http(void){for(unsigned i=0;i<USER_HTTP_SLOTS;++i)if(G_user_http[i].request.used && text_prefix(G_user_http[i].request.url,NAVIGATION_ORIGIN "stalled"))return 1;return 0;}
static void embedded_browser_present(const uint32_t* pixels){
 static unsigned hover_frames;static uint64_t hover_deadline;
 uint32_t sample=pixels[64*window_width+208];
#ifdef USER_FRAME_TESTS
 static uint64_t frame_diag_time;
 if(browser_stage==81 && dihos_time_monotonic_ms()-frame_diag_time>1000){
  frame_diag_time=dihos_time_monotonic_ms();print("Frame pixels same/cross/sandbox/blocked ");
  hex(pixels[260*window_width+20]);hex(pixels[260*window_width+220]);hex(pixels[260*window_width+420]);hex(pixels[360*window_width+20]);print("\n");
 }
#endif
 static unsigned last=~0u;if(last!=browser_stage){last=browser_stage;print("Embedded browser stage ");hex(browser_stage);print(" sample ");hex(sample);print("\n");}
 unsigned images_ready=1;
#ifdef USER_CONSENT_TESTS
 if(browser_stage==0 && pixels[360*window_width+605]!=0xff24ac68)images_ready=0;
#endif
#ifdef USER_IMAGE_TESTS
 if(browser_stage==0){
  for(unsigned i=0;i<8;++i){uint32_t p=pixels[180*window_width+i*40+20];
   if(i<3){if(p!=0xff2060e0)images_ready=0;}
   else {unsigned red=(p>>16)&255,green=(p>>8)&255,blue=p&255;if(red<220||green>45||blue>30)images_ready=0;}
  }
  static unsigned reported_images;
  if(images_ready && !reported_images++){print("PASS: inline SVG, data SVG, external SVG and PNG/JPEG/GIF/BMP/WebP paint real pixels\n");}
 }
#endif
 #ifdef USER_BLOB_TESTS
 if(browser_stage==0 && pixels[280*window_width+605]!=0xff24ac68)images_ready=0;
#endif
#ifdef USER_FAILED_IMAGE_TESTS
 if(browser_stage==0 && pixels[305*window_width+605]!=0xff24ac68)images_ready=0;
#endif
#ifdef USER_CSS_ALLOCATION_TESTS
 if(browser_stage==0 && pixels[335*window_width+605]!=0xff24ac68)images_ready=0;
#endif
 if(browser_stage==0 && images_ready && sample==0xff2468ac && nav_styles && nav_scripts){
#ifdef USER_CONSENT_TESTS
  print("PASS: consent-shaped fetch chain: DOM cookie, bodyless POST, credentialed cross-origin 204 and delayed response.text completion\n");
#endif
#ifdef USER_BLOB_TESTS
  print("PASS: memory Blob, FileReader slice, blob XHR/fetch/image, revocation and cross-origin read denial; subsequent navigation remains live\n");
#endif
#ifdef USER_IMAGE_TESTS
  browser_stage=80;hover_frames=window_frames;hover_deadline=dihos_time_monotonic_ms()+100;
  for(unsigned i=0;i<20;++i)browser_send(SACX_BROWSER_MOUSE,600+i,440,0,0,0);
#else
  browser_stage=1;browser_send(SACX_BROWSER_RESIZE,640,400,0,0,0);
#endif
 }
 else if(browser_stage==80 && dihos_time_monotonic_ms()>=hover_deadline){
  require(window_frames==hover_frames,"plain pointer movement must not upload unchanged viewport frames");
  print("PASS: twenty plain pointer movements cause zero additional viewport uploads\n");
#ifdef USER_FRAME_TESTS
  browser_stage=81;
#else
  browser_stage=1;browser_send(SACX_BROWSER_RESIZE,640,400,0,0,0);
#endif
 }
#ifdef USER_FRAME_TESTS
 else if(browser_stage==81 && pixels[260*window_width+20]==0xff2468ac && pixels[260*window_width+220]==0xff2468ac && pixels[260*window_width+420]==0xff68ac24){
  browser_stage=82;browser_click(220,260);
 }
 else if(browser_stage==82 && pixels[260*window_width+220]==0xff24ac68 && pixels[245*window_width+605]==0xff24ac68){
  require(pixels[400*window_width+10]==0xffffffff && pixels[360*window_width+20]!=0xffff00ff,"cross-origin/sandbox origin and X-Frame-Options restrictions hold");
  print("PASS: native iframes paint, route clicks, run JavaScript and postMessage; cross-origin and sandbox origin isolation hold\n");
  browser_stage=1;browser_send(SACX_BROWSER_RESIZE,640,400,0,0,0);
 }
#endif
 else if(browser_stage==1 && window_height==400 && sample==0xff2468ac){browser_stage=2;browser_click(16,64);}
 else if(browser_stage==2 && nav_next && sample==0xff24ac68){browser_stage=3;browser_click(16,64);}
 else if(browser_stage==3 && nav_stalled){browser_stage=4;browser_send(SACX_BROWSER_STOP,0,0,0,0,0);}
 else if(browser_stage==4 && !pending_browser_http()){browser_stage=5;browser_send(SACX_BROWSER_RELOAD,0,0,0,0,0);}
 else if(browser_stage==5 && nav_next==2 && sample==0xff24ac68){browser_stage=6;browser_send(SACX_BROWSER_NAVIGATE,0,0,0,0,NAVIGATION_ORIGIN "typed");}
 else if(browser_stage==6 && nav_typed && sample==0xffac2468){browser_stage=7;browser_click(16,114);browser_send(SACX_BROWSER_KEY,0x0e,'k',0,0,0);}
 else if(browser_stage==7 && !nav_form && sample==0xff2468ac){nav_form=1;browser_send(SACX_BROWSER_KEY,0x0f,'l',0,0,0);}
 else if(browser_stage==7 && nav_form && sample==0xff68ac24){browser_stage=8;browser_send(SACX_BROWSER_BACK,0,0,0,0,0);}
 else if(browser_stage==8 && sample==0xff24ac68){browser_stage=9;browser_send(SACX_BROWSER_FORWARD,0,0,0,0,0);}
 else if(browser_stage==9 && (sample==0xffac2468 || sample==0xff68ac24)){browser_stage=10;browser_send(SACX_BROWSER_NAVIGATE,0,0,0,0,NAVIGATION_ORIGIN "viewport");}
 else if(browser_stage==10 && sample==0xff123456){browser_stage=11;browser_send(SACX_BROWSER_RESIZE,1280,600,0,0,0);}
 else if(browser_stage==11 && window_width==1280 && sample==0xff654321 && pixels[50*window_width+1100]==0xff654321 && pixels[4*window_width+4]==0xffab12cd){browser_stage=12;browser_send(SACX_BROWSER_MOUSE,100,100,0,-16,0);}
 else if(browser_stage==12 && pixels[100*window_width+100]==0xff456789 && pixels[4*window_width+4]==0xffab12cd){browser_stage=13;browser_send(SACX_BROWSER_NAVIGATE,0,0,0,0,NAVIGATION_ORIGIN "fail-once");}
 else if(browser_stage==13 || browser_stage==15){
  sacx_browser_info info={};require(!dihos_browser_status(1,browser_token,&info),"read failed navigation state");
  if(info.state==SACX_BROWSER_FAILED && text_prefix(info.title,"Unable to load page") &&
     pixels[0]==0xfff4f6f8 && pixels[140*window_width+640]==0xffffffff &&
     (browser_stage==13 || text_prefix(info.url,NAVIGATION_ORIGIN "fail-once?again"))){
   require(text_prefix(info.url,NAVIGATION_ORIGIN "fail-once") && text_prefix(info.error,"Modeled DNS failure"),"error page preserves failed URL and detailed reason");
   if(browser_stage==13){
    unsigned retry=0;
    for(unsigned y=1;y+12<window_height && !retry;++y)for(unsigned x=1;x+12<window_width;++x)
     if(pixels[y*window_width+x]==0xff2459b7){retry=1;browser_click(x+8,y+8);break;}
    require(retry,"error page paints clickable Try again button");browser_stage=14;
   }else {browser_stage=16;browser_send(SACX_BROWSER_RELOAD,0,0,0,0,0);}
  }
 }
 else if(browser_stage==14 && sample==0xff19aa77){require(nav_failures==2,"Try again retries failed URL");browser_stage=15;browser_send(SACX_BROWSER_NAVIGATE,0,0,0,0,NAVIGATION_ORIGIN "fail-once?again");}
 else if(browser_stage==16 && sample==0xff19aa77){browser_stage=17;require(nav_failures==4,"reload retries failed HTTP URL");
#ifdef USER_CONSENT_TESTS
  require(region_presentations>0,"real renderer uses atomic region uploads for small pixel changes");
  print("PASS: native WebCore presents small changes through bounded atomic regions beside full frames\n");
#endif
  require(!dihos_browser_close(1,browser_token),"close existing browser frontend session after error recovery");}
}
#endif
static int native_window_create(uint64_t id,const sacx_user_window_desc *d,const uint32_t *pixels){
    require((d->width==640 || d->width==1280) && (d->height==480 || d->height==400 || d->height==600) && pixels[0]==0xffffffff,"owned window dimensions and initialized kernel backing");window_id=id;window_height=d->height;window_width=d->width;last_window_pixels=0;return 0;
}
static void native_window_present(uint64_t id,const uint32_t *pixels){
    require(id==window_id,"present owns window handle");++window_frames;
#ifdef USER_NATIVE_PAGE_EMBEDDED
    if(last_window_pixels==pixels)++region_presentations;
    last_window_pixels=pixels;embedded_browser_present(pixels);return;
#endif
    sacx_user_window_event e={.x=16,.y=48,.time_ms=dihos_time_monotonic_ms()};
#ifdef USER_NATIVE_PAGE_NAVIGATION
    e.y=100;
    static unsigned last_navigation_stage=~0u;
    if(last_navigation_stage!=nav_green){last_navigation_stage=nav_green;print("Browser controls stage ");hex(nav_green);print(" sample ");hex(pixels[100*640+208]);print("\n");}
    if(!nav_clicked && pixels[100*640+208]==0xff2468ac && nav_styles && nav_scripts){
        nav_clicked=1;
        e.type=SACX_WINDOW_DOWN;e.buttons=1;require(!dihos_user_window_push(id,&e),"queue fetched-page link press");
        e.type=SACX_WINDOW_UP;e.buttons=0;require(!dihos_user_window_push(id,&e),"queue fetched-page link release");
    }else if(nav_clicked && nav_next && !nav_green && pixels[100*640+208]==0xff24ac68){
        nav_green=1;
        e.type=SACX_WINDOW_DOWN;e.buttons=1;require(!dihos_user_window_push(id,&e),"queue stalled link press");
        e.type=SACX_WINDOW_UP;e.buttons=0;require(!dihos_user_window_push(id,&e),"queue stalled link release");
    }else if(nav_green==1 && nav_stalled){
        nav_green=2;e.x=112;e.y=20;
        e.type=SACX_WINDOW_DOWN;e.buttons=1;require(!dihos_user_window_push(id,&e),"queue Stop press during stalled load");
        e.type=SACX_WINDOW_UP;e.buttons=0;require(!dihos_user_window_push(id,&e),"queue Stop release during stalled load");
    }else if(nav_green==2){
        unsigned pending=0;for(unsigned i=0;i<USER_HTTP_SLOTS;++i)if(G_user_http[i].request.used && text_prefix(G_user_http[i].request.url,NAVIGATION_ORIGIN "stalled"))pending=1;
        if(!pending){
            nav_green=3;e.x=80;e.y=20;
            e.type=SACX_WINDOW_DOWN;e.buttons=1;require(!dihos_user_window_push(id,&e),"queue Reload press after cancellation");
            e.type=SACX_WINDOW_UP;e.buttons=0;require(!dihos_user_window_push(id,&e),"queue Reload release after cancellation");
        }
    }else if(nav_green==3 && nav_next==2 && pixels[100*640+208]==0xff24ac68){
        nav_green=4;
        e.type=SACX_WINDOW_KEY_DOWN;e.x=0x0f;e.y=0;e.delta=2;require(!dihos_user_window_push(id,&e),"focus address with Ctrl+L");
        const char* address=NAVIGATION_ORIGIN "typed";
        for(unsigned i=0;address[i];++i){e.x=4;e.y=address[i];e.delta=0;require(!dihos_user_window_push(id,&e),"type owned address");}
        e.x=0x28;e.y=0;require(!dihos_user_window_push(id,&e),"submit address with Enter");
    }else if(nav_green==4 && nav_typed && pixels[100*640+208]==0xffac2468){
        nav_green=5;e.x=16;e.y=150;
        e.type=SACX_WINDOW_DOWN;e.buttons=1;require(!dihos_user_window_push(id,&e),"focus native form input");
        e.type=SACX_WINDOW_UP;e.buttons=0;require(!dihos_user_window_push(id,&e),"release native form input");
        e.type=SACX_WINDOW_KEY_DOWN;e.x=0x0e;e.y='k';e.delta=0;
        require(!dihos_user_window_push(id,&e),"dispatch keydown to document JavaScript");
    }else if(nav_green==5 && !nav_form && pixels[100*640+208]==0xff2468ac){
        nav_form=1;e.type=SACX_WINDOW_KEY_DOWN;e.x=0x0f;e.y='l';e.delta=0;
        require(!dihos_user_window_push(id,&e),"DOM preventDefault blocks native text insertion");
    }else if(nav_green==5 && nav_form && pixels[100*640+208]==0xff68ac24){
        nav_green=6;e.x=16;e.y=20;
        e.type=SACX_WINDOW_DOWN;e.buttons=1;require(!dihos_user_window_push(id,&e),"Back to previous document");
        e.type=SACX_WINDOW_UP;e.buttons=0;require(!dihos_user_window_push(id,&e),"release Back");
    }else if(nav_green==6 && pixels[100*640+208]==0xff24ac68){
        nav_green=7;e.x=48;e.y=20;
        e.type=SACX_WINDOW_DOWN;e.buttons=1;require(!dihos_user_window_push(id,&e),"Forward to typed document");
        e.type=SACX_WINDOW_UP;e.buttons=0;require(!dihos_user_window_push(id,&e),"release Forward");
    }else if(nav_green==7 && (pixels[100*640+208]==0xffac2468 || pixels[100*640+208]==0xffac6824 || pixels[100*640+208]==0xff2468ac || pixels[100*640+208]==0xff68ac24)){
        nav_green=8;
        e.type=SACX_WINDOW_DOWN;e.buttons=1;require(!dihos_user_window_push(id,&e),"queue another stalled navigation press");
        e.type=SACX_WINDOW_UP;e.buttons=0;require(!dihos_user_window_push(id,&e),"queue another stalled navigation release");
    }else if(nav_green==8){
        unsigned pending=0;for(unsigned i=0;i<USER_HTTP_SLOTS;++i)if(G_user_http[i].request.used && text_prefix(G_user_http[i].request.url,NAVIGATION_ORIGIN "stalled"))pending=1;
        if(pending){nav_green=9;e.type=SACX_WINDOW_CLOSE;require(!dihos_user_window_push(id,&e),"close native window during pending HTTP");}
    }
#else
    if(window_frames==1){
        require(pixels[48*640+16]==0xff2468ac,"first real WebCore surface reached presentation");
        e.type=SACX_WINDOW_DOWN;e.buttons=1;require(!dihos_user_window_push(id,&e),"queue native click press");
        e.type=SACX_WINDOW_UP;e.buttons=0;require(!dihos_user_window_push(id,&e),"queue native click release");
    }else if(window_frames==2){
        require(pixels[48*640+16]==0xff24ac68,"DOM click JavaScript changed next presented surface");
        e.type=SACX_WINDOW_CLOSE;require(!dihos_user_window_push(id,&e),"queue native window close");
    }
#endif
}
static void native_window_destroy(uint64_t id){require(id==window_id,"destroy owns window handle");++window_closed;last_window_pixels=0;}
#endif
void qemu_main(void){
    // Test-only RTC anchor; the production firmware supplies this on the guest.
    dihos_time_init_utc(1700000000,1);
    print("DihOS ARM64 user runtime test\n");memory_init();
    aa64_user_timer_config config={0x08000000,USER_TEST_GICR_BASE,0x20000,27,USER_TEST_TIMER_FLAGS};
    int rc=aa64_user_timer_init(&config);if(rc){print("timer init ");hex((uint64_t)rc);print("\n");}require(rc==0,"GICv3 timer init");
    uint32_t icfgr=*(volatile uint32_t*)(uintptr_t)(0x080a0000+0x10000+0xc04);
    require(((icfgr>>22)&3)==((USER_TEST_TIMER_FLAGS&1)?2u:0u),"GIC timer trigger matches GTDT flags");
    aarch64_user_vm vm;require(!aarch64_user_vm_create(&vm),"VM create");
    uint32_t *code=pmem_alloc_pages(1);void *stack=pmem_alloc_pages(1);require(code&&stack,"process pages");
    require(!aarch64_user_vm_map(&vm,0x100000000ull,(uintptr_t)code,4096,AARCH64_USER_VM_READ|AARCH64_USER_VM_EXECUTE),"RX code map");
    require(!aarch64_user_vm_map(&vm,0x100010000ull,(uintptr_t)stack,4096,AARCH64_USER_VM_READ|AARCH64_USER_VM_WRITE),"RW stack map");
    static aa64_user_cpu cpu;
    const uint32_t loop[]={0x91000400,0x17ffffff}; /* add x0,x0,#1; b -4 */
    install(code,loop,2,&cpu,&vm);
    for(unsigned i=1;i<31;++i)cpu.frame.fault.x[i]=0x1122334400000000ull+i;
    for(unsigned i=0;i<64;++i)cpu.frame.q[i]=0xaabbccdd00000000ull+i;
    cpu.frame.fpcr=0x800000;cpu.frame.fpsr=0x10;cpu.frame.tpidr=0x12345678;
    uint64_t before=0;for(unsigned i=0;i<100;++i){
        require(aa64_user_slice_run(&cpu,200)==AA64_USER_PREEMPTED,"API-free loop preemption");
        /* An emulated CPU may be descheduled between timer activation and
         * ERET. That slice can expire without executing a user instruction. */
        require(cpu.frame.fault.x[0]>=before,"resume preserves loop progress");before=cpu.frame.fault.x[0];
        uint64_t root;__asm__ volatile("mrs %0,ttbr0_el1":"=r"(root));require(root==(uintptr_t)boot_root&&!aa64_user_slice_active(),"kernel root restored");
        for(unsigned r=1;r<31;++r)require(cpu.frame.fault.x[r]==0x1122334400000000ull+r,"GPR preserved");
        for(unsigned r=0;r<64;++r)require(cpu.frame.q[r]==0xaabbccdd00000000ull+r,"full SIMD preserved");
        require(cpu.frame.fpcr==0x800000&&cpu.frame.fpsr==0x10&&cpu.frame.tpidr==0x12345678,"FP control and TLS preserved");
    }
    require(before>0,"user loop makes progress across timer slices");
    print("PASS: 100 timer preemptions of an API-free EL0 loop; GPR/SIMD/FP/TLS and kernel root restored\n");
    const uint32_t exit_program[]={0xd28009a0,0xd2800008,0xd4000001,0x14000000};
    install(code,exit_program,4,&cpu,&vm);require(run_to_trap(&cpu)==AA64_USER_EXITED&&cpu.exit_status==77,"syscall exit");
    uint64_t legacy_status=0;
    require(!aa64_user_enter(&vm,0x100000000ull,0x100011000ull,&legacy_status)&&legacy_status==77,"legacy EL0 syscall vector compatibility");
    print("PASS: existing synchronous EL0 entry/exit uses the shared vector correctly\n");
    const uint32_t fault_program[]={0xf94003e0,0x14000000}; /* ldr x0,[sp]; b . (upper stack guard) */
    install(code,fault_program,2,&cpu,&vm);require(run_to_trap(&cpu)==AA64_USER_FAULTED,"stack guard fault contained");
    require(cpu.frame.fault.far==0x100011000ull,"guard fault address");
    const uint32_t kernel_read[]={0xd2a80001,0xf9400020,0x14000000}; /* x1=0x40000000; ldr x0,[x1] */
    install(code,kernel_read,3,&cpu,&vm);require(run_to_trap(&cpu)==AA64_USER_FAULTED,"EL0 kernel access denied");
    const uint32_t sleep_program[]={0xd2800140,0xd2800048,0xd4000001,0xd2800b00,0xd2800008,0xd4000001,0x14000000};
    install(code,sleep_program,7,&cpu,&vm);require(run_to_trap(&cpu)==AA64_USER_SLEEPING&&cpu.sleep_ms==10,"sleep syscall suspends");
    require(run_to_trap(&cpu)==AA64_USER_EXITED&&cpu.exit_status==88,"sleep continuation resumes");
    const uint32_t rx_write[]={0x10000001,0xf9000020,0x14000000};
    install(code,rx_write,3,&cpu,&vm);require(run_to_trap(&cpu)==AA64_USER_FAULTED,"RX code write denied");
    install(code,loop,2,&cpu,&vm);cpu.frame.fault.elr=0x100010000ull;require(run_to_trap(&cpu)==AA64_USER_FAULTED,"RW stack execution denied");
    print("PASS: user exit, guard fault, kernel access fault and resumable sleep syscall\n");
    aarch64_user_vm_release(&vm);pmem_free_pages(code,1);pmem_free_pages(stack,1);require(!allocations,"process and page tables reclaimed");
    print("PASS: all owned process pages reclaimed; kernel remains running after user faults\n");
    /* Corrupt an unused inherited leaf, not the executing kernel/vector leaf.
     * Revoking EL1 instruction permission there can fault the rejection test
     * itself before vm_kernel_only() can inspect it (layout/TLB dependent). */
#ifdef USER_TEST_FIRMWARE_L2
    uint64_t inherited_leaf=boot_ram_l2[511];
    boot_ram_l2[511]=inherited_leaf|(1ull<<6);
#else
    uint64_t inherited_leaf=linear_root[511];
    linear_root[511]=(511ull<<30)|1ull|(1ull<<6)|(1ull<<10)|(1ull<<53)|(1ull<<54);
#endif
    require(aarch64_user_vm_create(&vm)<0 && !allocations,"inherited EL0-accessible kernel map rejected");
#ifdef USER_TEST_FIRMWARE_L2
    boot_ram_l2[511]=inherited_leaf;
#else
    linear_root[511]=inherited_leaf;
#endif
    /* The guest's firmware maps the candidate aliases already. Model an
     * entirely mapped 39-bit TTBR0 so admission must use owned backing VAs. */
    for(unsigned i=2;i<512;++i)linear_root[i]=((uint64_t)i<<30)|1ull|(1ull<<10)|(3ull<<8)|(1ull<<54);
#ifdef USER_TEST_FIRMWARE_L0
    for(unsigned i=0;i<512;++i)boot_upper_l1[i]=((512ull+i)<<30)|1ull|(1ull<<10)|(3ull<<8)|(1ull<<54);
    boot_root[1]=(uintptr_t)boot_upper_l1|3;
#endif
    __asm__ volatile("dsb ishst; tlbi vmalle1; dsb ish; isb":::"memory");
    require(!aarch64_user_vm_create(&vm),"fully mapped VM clone");
    void *guarded_code=pmem_alloc_pages(3),*guarded_stack=pmem_alloc_pages(3);
    require(guarded_code&&guarded_stack,"owned payloads with guard neighbors");
    code=(uint32_t*)((uint8_t*)guarded_code+4096);stack=(uint8_t*)guarded_stack+4096;
    uint64_t code_va=(uintptr_t)code,stack_va=(uintptr_t)stack;
    require(!aarch64_user_vm_map(&vm,code_va,code_va,4096,AARCH64_USER_VM_READ|AARCH64_USER_VM_EXECUTE),"private RX backing map");
    require(!aarch64_user_vm_map(&vm,stack_va,stack_va,4096,AARCH64_USER_VM_READ|AARCH64_USER_VM_WRITE),"private RW backing map");
    uint8_t copy_source[16],copy_result[16];for(unsigned i=0;i<16;++i)copy_source[i]=(uint8_t)(i+1);
    require(!aarch64_user_vm_copy_out(&vm,stack_va,copy_source,16) && !aarch64_user_vm_copy_in(&vm,copy_result,stack_va,16),"checked user buffer round trip");
    for(unsigned i=0;i<16;++i)require(copy_source[i]==copy_result[i],"user buffer bytes preserved");
    require(aarch64_user_vm_copy_out(&vm,code_va,copy_source,16)==-14,"copy-out cannot write executable code");
    require(aarch64_user_vm_copy_in(&vm,copy_result,0x40000000,16)==-14,"copy-in cannot read kernel pages");
    for(unsigned i=0;i<8;++i)((uint8_t*)stack)[4096-8+i]=0xaa;
    require(aarch64_user_vm_copy_out(&vm,stack_va+4096-8,copy_source,16)==-14,"cross-guard output rejected");
    for(unsigned i=0;i<8;++i)require(((uint8_t*)stack)[4096-8+i]==0xaa,"failed output leaves first page unchanged");
    require(aarch64_user_vm_copy_in(&vm,copy_result,UINT64_MAX-3,8)==-14,"overflowing user range rejected");
    uint64_t guard_walk[4];
    require(aarch64_user_vm_describe(&vm,stack_va-8,guard_walk)==3 && !(guard_walk[3]&(1ull<<52)),"split leaves do not inherit contiguous hints");
#ifdef USER_TEST_FIRMWARE_L2
    for(unsigned n=0;n<vm.owned_table_count;++n){const uint64_t *table=vm.owned_tables[n];
        for(unsigned i=0;i<512;++i)require(!(table[i]&(1ull<<52)),"private cloned contiguous groups unfolded");
    }
    require((boot_ram_l2[1]&(1ull<<52))!=0,"firmware contiguous hints remain unchanged");
#endif
    install(code,exit_program,4,&cpu,&vm);cpu.frame.fault.elr=code_va;cpu.frame.sp=stack_va+4096;
    require(run_to_trap(&cpu)==AA64_USER_EXITED && cpu.exit_status==77,"owned backing code executes");
    install(code,fault_program,2,&cpu,&vm);cpu.frame.fault.elr=code_va;cpu.frame.sp=stack_va+4096;
    require(run_to_trap(&cpu)==AA64_USER_FAULTED && cpu.frame.fault.far==stack_va+4096,"owned backing upper stack guard");
    const uint32_t lower_guard[]={0xf9400020,0x14000000};
    install(code,lower_guard,2,&cpu,&vm);cpu.frame.fault.elr=code_va;cpu.frame.sp=stack_va+4096;cpu.frame.fault.x[1]=stack_va-8;
    require(run_to_trap(&cpu)==AA64_USER_FAULTED && cpu.frame.fault.far==stack_va-8,"owned backing lower stack guard");
    install(code,rx_write,3,&cpu,&vm);cpu.frame.fault.elr=code_va;cpu.frame.sp=stack_va+4096;
    require(run_to_trap(&cpu)==AA64_USER_FAULTED,"owned backing RX write denied");
    install(code,loop,2,&cpu,&vm);cpu.frame.fault.elr=stack_va;cpu.frame.sp=stack_va+4096;
    require(run_to_trap(&cpu)==AA64_USER_FAULTED,"owned backing stack execution denied");
    require(linear_root[1]==boot_ram_descriptor,"active kernel mapping unchanged by private splits");
    aarch64_user_vm_release(&vm);pmem_free_pages(guarded_code,3);pmem_free_pages(guarded_stack,3);require(!allocations,"guarded backing cleanup");
    print("PASS: owned backing RX/RW protection, both stack guards and unchanged kernel mappings\n");
    /* Exercise production heap ownership against actual private page tables. */
    for(unsigned failure=0;failure<5;++failure){
        require(!aarch64_user_vm_create(&vm),"heap failure VM");
        dihos_user_memory heap={0};heap.limit=65536;
        unsigned before=allocations,tables=vm.owned_table_count;
        uint64_t request[3]={16384,0,0};fail_after=(int)failure;
        int64_t address=dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_RESERVE,request);fail_after=-1;
        if(address<0)require(allocations==before && vm.owned_table_count==tables && !heap.charged,"heap prepare failure rolls back tables and backing");
        aarch64_user_vm_release(&vm);dihos_user_memory_destroy(&heap);require(!allocations,"failed reservation cleanup");
    }
    require(!aarch64_user_vm_create(&vm),"heap VM");
    aarch64_user_vm foreign_vm;require(!aarch64_user_vm_create(&foreign_vm),"foreign heap VM");
    dihos_user_memory heap={0},foreign={0};heap.limit=32768;foreign.limit=32768;
    uint64_t request[3]={16384,0,0};
    int64_t reserved=dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_RESERVE,request);
    require(reserved>0 && heap.reserved==16384 && heap.charged==24576,"reservation accounts physical guard pages");
    uint64_t address=(uint64_t)reserved;
    require(aarch64_user_vm_copy_in(&vm,copy_result,address,1)==-14,"reservation inaccessible until commit");
    require(dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_RESERVE,request)==-12,"heap quota enforced");
    request[0]=address;request[1]=0;
    require(dihos_user_memory_call(&foreign,&foreign_vm,SACX_USER_MEMORY_RELEASE,request)==-22,"cannot release foreign allocation");
    request[1]=4096;request[2]=3;
    require(dihos_user_memory_call(&foreign,&foreign_vm,SACX_USER_MEMORY_PROTECT,request)==-14,"cannot protect foreign allocation");
    require(dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_PROTECT,request)==-14,"cannot protect uncommitted memory");
    memset((void*)(uintptr_t)address,0xa5,16384);
    require(!dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_COMMIT,request),"heap commit");
    for(unsigned i=0;i<4096;++i)require(!((uint8_t*)(uintptr_t)address)[i],"commit clears previous physical contents");
    require(heap.committed==4096 && heap.peak==4096,"heap committed and peak accounting");
    require(dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_COMMIT,request)==-17,"duplicate commit rejected without resetting data");
    require(aarch64_user_vm_copy_in(&vm,copy_result,address+4096,1)==-14,"adjacent uncommitted page remains private");
    require(aarch64_user_vm_copy_in(&vm,copy_result,address-1,1)==-14 && aarch64_user_vm_copy_in(&vm,copy_result,address+16384,1)==-14,"heap guard pages inaccessible");
    require(!aarch64_user_vm_copy_out(&vm,address,copy_source,16),"heap writable after commit");
    request[2]=1;require(!dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_PROTECT,request),"heap read-only transition");
    require(aarch64_user_vm_copy_out(&vm,address,copy_source,16)==-14 && !aarch64_user_vm_copy_in(&vm,copy_result,address,16),"read-only heap denies kernel service writes");
    request[2]=0;require(!dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_PROTECT,request),"heap no-access transition");
    require(aarch64_user_vm_copy_in(&vm,copy_result,address,1)==-14,"no-access heap denies reads");
    request[2]=7;require(dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_PROTECT,request)==-22,"executable heap rejected");
    request[2]=3;require(!dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_PROTECT,request),"heap restore RW");
    install((uint32_t*)(uintptr_t)address,loop,2,&cpu,&vm);cpu.frame.fault.elr=address;cpu.frame.sp=address+4096;
    require(run_to_trap(&cpu)==AA64_USER_FAULTED,"heap execution faults in hardware");
    request[0]=address+4096;request[1]=request[2]=0;
    require(dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_RELEASE,request)==-22,"interior free rejected");
    request[0]=address;require(!dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_RELEASE,request),"heap release");
    require(!heap.reserved && !heap.committed && !heap.charged && heap.peak==4096,"released heap accounting");
    require(aarch64_user_vm_copy_in(&vm,copy_result,address,1)==-14,"released mapping revoked");
    require(dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_RELEASE,request)==-22,"double release rejected");
    aarch64_user_vm_release(&vm);aarch64_user_vm_release(&foreign_vm);dihos_user_memory_destroy(&heap);dihos_user_memory_destroy(&foreign);
    require(!allocations,"heap/VM ownership cleanup");
    require(!aarch64_user_vm_create(&vm),"full heap bitmap VM");
    heap=(dihos_user_memory){0};heap.limit=128*1024*1024;
    request[0]=DIHOS_USER_MEMORY_REGION_MAX;request[1]=request[2]=0;
    reserved=dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_RESERVE,request);require(reserved>0,"maximum heap region reservation");
    address=(uint64_t)reserved;
    memset((void*)(uintptr_t)address,0xa5,DIHOS_USER_MEMORY_REGION_MAX);
    for(unsigned i=0;i<DIHOS_USER_MEMORY_REGION_MAX/65536;++i){request[0]=address+i*65536;request[1]=65536;request[2]=3;
        require(!dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_COMMIT,request),"commit across full 64-page bitmap");}
    for(unsigned i=0;i<DIHOS_USER_MEMORY_REGION_MAX/4096/64;++i)require(heap.regions[0].committed[i]==UINT64_MAX,"full multiword commit bitmap");
    for(unsigned i=0;i<DIHOS_USER_MEMORY_REGION_MAX;i+=4096)require(!((uint8_t*)(uintptr_t)address)[i],"all large region pages zeroed");
    require(heap.committed==DIHOS_USER_MEMORY_REGION_MAX,"last page and full commit bitmap");
    request[0]=address+4095ull*4096;request[1]=2*4096;request[2]=1;
    require(!dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_PROTECT,request),"protect across commitment bitmap words");
    require(aarch64_user_vm_copy_out(&vm,request[0],copy_source,1)==-14 && aarch64_user_vm_copy_out(&vm,request[0]+4096,copy_source,1)==-14,"both cross-word pages become read-only");
    request[2]=3;require(!dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_PROTECT,request),"restore cross-word pages");
    memset((void*)(uintptr_t)request[0],0xa5,8192);request[2]=0;
    require(!dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_DECOMMIT,request),"decommit across bitmap words");
    require(heap.committed==DIHOS_USER_MEMORY_REGION_MAX-8192 &&
        aarch64_user_vm_copy_in(&vm,copy_result,request[0],1)==-14 &&
        aarch64_user_vm_copy_in(&vm,copy_result,request[0]+4096,1)==-14,"decommit revokes both pages and accounts them");
    require(dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_DECOMMIT,request)==-14,"duplicate kernel decommit rejected");
    request[0]-=4096;
    require(dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_DECOMMIT,request)==-14 &&
        !aarch64_user_vm_copy_in(&vm,copy_result,request[0],1),"mixed decommit rejects before revoking any committed page");
    request[0]+=4096;request[2]=3;
    require(dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_DECOMMIT,request)==-22,"decommit does not accept permission flags");
    request[2]=0;
    require(dihos_user_memory_call(&foreign,&vm,SACX_USER_MEMORY_DECOMMIT,request)==-14,"foreign decommit rejected");
    request[2]=3;require(!dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_COMMIT,request),"recommit decommitted pages");
    for(unsigned i=0;i<8192;++i)require(!((uint8_t*)(uintptr_t)request[0])[i],"recommit zeroes previous data");
    request[0]=UINT64_MAX&~4095ull;request[1]=4096;request[2]=3;
    require(dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_PROTECT,request)==-14,"wrapping foreign range rejected");
    request[0]=address;request[1]=65536+4096;
    require(dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_PROTECT,request)==-7,"permission update work bounded");
    for(unsigned i=1;i<DIHOS_USER_MEMORY_REGIONS;++i){request[0]=4096;request[1]=request[2]=0;
        require(dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_RESERVE,request)>0,"bounded region slots");}
    require(dihos_user_memory_call(&heap,&vm,SACX_USER_MEMORY_RESERVE,request)==-12,"exhausted region slots rejected");
    aarch64_user_vm_release(&vm);dihos_user_memory_destroy(&heap);require(!allocations,"full region/slot cleanup");
    require(DIHOS_USER_MEMORY_REGIONS==32 && DIHOS_USER_SPARSE_REGIONS==16,"independent bounded heap and sparse slot tables");
    print("PASS: 32 owned heap regions, bounded exhaustion and complete cleanup; sparse VM remains at 16 slots\n");
    print("PASS: owned heap quota, zeroing, guards, foreign/interior/double free rejection, transactional failures and hardware NX\n");
    require(!dihos_user_runtime_enable(),"runtime enable and finite timer-delivery preflight");
    require(published_pages>0,"private page tables explicitly published");
    dihos_process_handle handles[4],extra;
    for(unsigned kind=1;kind<=4;++kind)require(!dihos_user_spawn_fixture(kind,&handles[kind-1]),"spawn fixture");
    require(dihos_user_spawn_fixture(1,&extra)<0,"bounded task slots");
    unsigned kernel_frames=0;
    for(unsigned n=0;n<100;++n){dihos_user_runtime_poll();++kernel_frames;}
    dihos_user_task_info infos[4];require(dihos_user_runtime_snapshot(infos,4)==4,"task snapshots");
    require(infos[0].state==DIHOS_USER_READY && infos[0].preemptions>0 && infos[0].counter>0,"loop continues beside kernel frames");
    require(infos[0].backing_alias && infos[0].code_va && infos[0].stack_va,"fully mapped firmware uses owned backing aliases");
    require(infos[1].state==DIHOS_USER_DONE && infos[1].exit_status==77,"normal task exit");
    require(infos[2].state==DIHOS_USER_FAILED && infos[2].esr,"fault is isolated");
    require(infos[3].state==DIHOS_USER_DONE && infos[3].exit_status==88,"sleep wakes without blocking other tasks");
    require(kernel_frames==100,"desktop gets control after each quantum");
    require(!dihos_user_runtime_stop(handles[0]),"cancel API-free loop");require(!allocations,"all task resources reclaimed");
    for(unsigned attempt=0;attempt<10;++attempt){
        fail_after=(int)attempt;int admission=dihos_user_spawn_fixture(1,&extra);fail_after=-1;
        if(!admission)dihos_user_runtime_stop(extra);
        else {
            require(dihos_user_runtime_snapshot(infos,4)>0,"rejected admission retained for diagnosis");
            require(infos[0].admission_result && infos[0].admission_step>=1 && infos[0].admission_step<=10,"admission step and underlying error preserved");
        }
        require(!allocations,"allocation failure cleanup");
    }
    for(unsigned repeat=0;repeat<100;++repeat){
        require(!dihos_user_spawn_fixture(1,&extra),"repeated create");
        require(dihos_user_runtime_stop(handles[0])<0,"stale generation rejected");
        dihos_user_runtime_poll();require(!dihos_user_runtime_stop(extra),"repeated cancel");require(!allocations,"repeated close cleanup");
    }
    for(unsigned i=2;i<512;++i)linear_root[i]=0;
#ifdef USER_TEST_FIRMWARE_L0
    boot_root[1]=0;
#endif
    __asm__ volatile("dsb ishst; tlbi vmalle1; dsb ish; isb":::"memory");
    require(!dihos_user_spawn_fixture(1,&extra),"sparse firmware still admits fixture");
    require(dihos_user_runtime_snapshot(infos,4)>0 && !infos[0].backing_alias,"sparse firmware prefers spare alias");
    dihos_user_runtime_poll();require(!dihos_user_runtime_stop(extra) && !allocations,"spare alias task cleanup");
    dihos_process_handle loaded[3];
    unsigned source_pages=(sizeof(user_exit_elf)+4095)/4096;
    uint8_t *source=pmem_alloc_pages(source_pages);require(source!=0,"owned ELF staging input");
    memcpy(source,user_exit_elf,sizeof(user_exit_elf));
    require(!dihos_user_spawn_elf(source,sizeof(user_exit_elf),&loaded[0]),"load separately compiled ELF");
    memset(source,0,sizeof(user_exit_elf));pmem_free_pages(source,source_pages);
    require(!dihos_user_spawn_elf(user_loop_elf,sizeof(user_loop_elf),&loaded[1]),"load API-free ELF loop");
    require(!dihos_user_spawn_elf(user_fault_elf,sizeof(user_fault_elf),&loaded[2]),"load faulting ELF");
    for(unsigned i=0;i<100;++i)dihos_user_runtime_poll();
    require(dihos_user_runtime_snapshot(infos,4)>=3,"ELF task status");
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==42,"ELF data/BSS, yield, sleep and exit; staging lifetime independent");
    require(infos[1].state==DIHOS_USER_READY && infos[1].preemptions>0,"ELF API-free loop preempted");
    require(infos[2].state==DIHOS_USER_FAILED && infos[2].far==0,"ELF fault contained");
    require(infos[2].fault_level==1 && (infos[2].fault_walk[1]&1) && !(infos[2].fault_walk[1]&2) && infos[2].tcr!=0,"fault descriptors and TCR retained after private tables freed");
    require(infos[2].hardware_root==infos[2].expected_root && infos[2].hardware_root!=(uintptr_t)boot_root && (infos[2].hardware_par&1),"live user root and hardware permission failure recorded before restoring kernel root");
    dihos_user_runtime_stop_all();require(!allocations,"ELF image, stack and page tables reclaimed");
    for(unsigned n=0;n<16;++n){fail_after=(int)n;int rc=dihos_user_spawn_elf(user_exit_elf,sizeof(user_exit_elf),&extra);fail_after=-1;
        if(!rc)dihos_user_runtime_stop(extra);require(!allocations,"ELF allocation failure cleanup");
    }
    for(unsigned n=0;n<50;++n){
        require(!dihos_user_spawn_elf(user_loop_elf,sizeof(user_loop_elf),&extra),"repeated ELF load");
        dihos_user_runtime_poll();require(!dihos_user_runtime_stop(extra) && !allocations,"repeated ELF cancellation cleanup");
    }
    require(!dihos_user_spawn_elf(user_sacx_elf,sizeof(user_sacx_elf),&extra),"load userspace-local SACX API table");
    uint64_t service_deadline=dihos_time_monotonic_ms()+500;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==73,"SACX user wrappers, invalid pointer rejection, clock, yield, sleep and exit");
    require(infos[0].sacx_calls>=10 && infos[0].granted_capabilities==0x1e4 && infos[0].last_log[0]=='S',"SACX kernel-owned grants and diagnostic text retained");
    require(!allocations,"SACX task cleanup");
    print("PASS: userspace SACX function table crosses versioned syscall boundary; checked buffers, clock, log, yield, sleep and exit\n");
    require(!dihos_user_spawn_elf(user_memory_elf,sizeof(user_memory_elf),&extra),"load SACX heap fixture");
    service_deadline=dihos_time_monotonic_ms()+3000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=74){print(infos[0].last_log);hex(infos[0].exit_status);}
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==74 && infos[0].memory_peak>=16384,"EL0 SACX heap fixture");
    require(!allocations && !infos[0].memory_reserved && !infos[0].memory_committed,"exit releases committed and reserved heap");
    require(!dihos_user_spawn_elf(user_memory_fault_elf,sizeof(user_memory_fault_elf),&extra),"load heap write fault fixture");
    service_deadline=dihos_time_monotonic_ms()+3000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    require(infos[0].state==DIHOS_USER_FAILED && infos[0].memory_peak==16384 && !allocations,"EL0 read-only write faults and reclaims heap");
    for(unsigned i=0;i<20;++i){
        require(!dihos_user_spawn_elf(user_memory_elf,sizeof(user_memory_elf),&extra),"heap cancellation fixture");
        // Timer preemption/host scheduling can occur before the first reserve.
        // Synchronize on ownership, rather than assuming three CPU slices.
        service_deadline=dihos_time_monotonic_ms()+3000;
        do{dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(!infos[0].memory_reserved && infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
        require(infos[0].memory_reserved>0,"cancellation actually owns heap backing");
        require(!dihos_user_runtime_stop(extra) && !allocations,"cancel task with live heap reservation");
    }
    print("PASS: EL0 SACX heap wrappers, page protection, repeated zeroed reuse, exit/fault/cancel cleanup\n");
    require(!dihos_user_spawn_elf(user_cpp_elf,sizeof(user_cpp_elf),&extra),"load freestanding C++ static PIE");
    service_deadline=dihos_time_monotonic_ms()+3000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=75){print(infos[0].last_log);print(" C++ status ");hex(infos[0].exit_status);print(" ESR ");hex(infos[0].esr);print(" PC ");hex(infos[0].pc);}
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==75 && infos[0].relocations>0 && infos[0].memory_peak==256*1024,"C++ relocations, startup constructors, initialized pointers, virtual dispatch and destructors");
    require(!allocations && !infos[0].memory_reserved,"C++ runtime heap reclaimed");
    print("PASS: freestanding C++ startup, RELATIVE relocations into RO data, constructors/destructors, initialized pointers, virtual dispatch and SACX new/delete\n");
    require(!dihos_user_spawn_elf(user_arena_elf,sizeof(user_arena_elf),&extra),"load arena allocator fixture");
    service_deadline=dihos_time_monotonic_ms()+10000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=76){print(infos[0].last_log);print(" arena status ");hex(infos[0].exit_status);print(" ESR ");hex(infos[0].esr);print(" PC ");hex(infos[0].pc);}
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==76 && infos[0].memory_peak>2*1024*1024,"arena fixture: pooled objects, aligned new, calloc/realloc, expanding pools, OOM and recovery");
    require(!allocations && !infos[0].memory_reserved && !infos[0].memory_committed,"arena fixture reclaims all pools");
    print("PASS: userspace TLSF arena: 1024 objects in one pool, aligned new/alloc, calloc/realloc, eight-pool exhaustion and trim\n");
    require(!dihos_user_spawn_elf(user_library_elf,sizeof(user_library_elf),&extra),"load pinned libc++ fixture");
    service_deadline=dihos_time_monotonic_ms()+10000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=77){print(infos[0].last_log);print(" library status ");hex(infos[0].exit_status);print(" ESR ");hex(infos[0].esr);}
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==77 && !allocations,"libc++ vector/string/sort/span/optional/ownership and cleanup");
    require(!dihos_user_spawn_elf(user_library_fail_elf,sizeof(user_library_fail_elf),&extra),"load libc++ hardening fixture");
    service_deadline=dihos_time_monotonic_ms()+10000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==151 && infos[0].last_log[0]=='l' && !allocations,"libc++ bounds assertion terminates only its task and reclaims memory");
    print("PASS: pinned target libc++ containers, strings, algorithms, ownership, bounds hardening and isolated abort cleanup\n");
    require(!dihos_user_spawn_elf(user_threads_elf,sizeof(user_threads_elf),&extra),"load native thread/TLS fixture");
    service_deadline=dihos_time_monotonic_ms()+10000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=78){print(infos[0].last_log);print(" thread status ");hex(infos[0].exit_status);print(" ESR ");hex(infos[0].esr);print(" FAR ");hex(infos[0].far);print(" PC ");hex(infos[0].pc);}
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==78 && infos[0].threads_created==25 && infos[0].thread_joins==25 && infos[0].thread_waits>=6 && infos[0].tls_bytes>0 && !allocations,"shared VM threads, native TLS, join, waits, allocator contention and repeated reap");
    dihos_user_elf_plan tls_plan;require(!dihos_user_elf_validate(user_threads_bss_elf,sizeof(user_threads_bss_elf),&tls_plan) && !tls_plan.tls_file_bytes && tls_plan.tls_memory_bytes,"BSS-only TLS metadata admitted");
    require(!dihos_user_spawn_elf(user_threads_bss_elf,sizeof(user_threads_bss_elf),&extra),"load BSS-only TLS fixture");
    service_deadline=dihos_time_monotonic_ms()+10000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==78 && !allocations,"BSS-only TLS remains private and is reset on reuse");
    require(!dihos_user_spawn_elf(user_threads_fault_elf,sizeof(user_threads_fault_elf),&extra),"load faulting worker fixture");
    service_deadline=dihos_time_monotonic_ms()+10000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    require(infos[0].state==DIHOS_USER_FAILED && (infos[0].fault_thread&255)!=0 && !allocations,"worker fault terminates only its process and releases shared heap/TCBs/stacks");
    require(!dihos_user_spawn_elf(user_threads_wait_elf,sizeof(user_threads_wait_elf),&extra),"load parked thread fixture");
    service_deadline=dihos_time_monotonic_ms()+3000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while((infos[0].state!=DIHOS_USER_WAITING || infos[0].threads_live!=4) && infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    require(infos[0].state==DIHOS_USER_WAITING && infos[0].threads_live==4,"four parked threads return control to kernel");
    require(!dihos_user_runtime_stop(extra) && !allocations,"cancel parked threads without waiting for their deadlines");
    require(!dihos_user_spawn_elf(user_threads_oom_elf,sizeof(user_threads_oom_elf),&extra),"load thread allocation-failure fixture");
    fail_after=0;service_deadline=dihos_time_monotonic_ms()+3000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    fail_after=-1;require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==79 && !allocations,"failed thread backing allocation leaves no resources");
    print("PASS: native threads/TLS, process-local timed join/wait/wake, shared allocator contention, worker faults, parked cancellation and OOM cleanup\n");
    require(!dihos_user_spawn_elf(user_threads_spin_elf,sizeof(user_threads_spin_elf),&extra),"load API-free worker loops");
    service_deadline=dihos_time_monotonic_ms()+3000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==80 && infos[0].preemptions>=3 && !allocations,"multiple API-free worker loops preempt and main sleep/join completes");
    print("PASS: three API-free worker loops preempt while main sleeps, resumes and joins; kernel regains control\n");
    for(unsigned repeat=0;repeat<5;++repeat){
        require(!dihos_user_spawn_elf(user_lifetime_elf,sizeof(user_lifetime_elf),&extra),"load compiler-generated C++ thread lifetimes");
        service_deadline=dihos_time_monotonic_ms()+3000;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=81){print(infos[0].last_log);print(" lifetime status ");hex(infos[0].exit_status);print(" ESR ");hex(infos[0].esr);print(" PC ");hex(infos[0].pc);}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==81 && infos[0].thread_joins==3 && infos[0].thread_waits>=3 && !allocations,"C++ guard publication, thread exit/join cleanup, reverse TLS destructors and concurrent global finalizers");
    }
    require(!dihos_user_spawn_elf(user_lifetime_recursive_elf,sizeof(user_lifetime_recursive_elf),&extra),"load recursive static initializer");
    service_deadline=dihos_time_monotonic_ms()+3000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==153 && !allocations,"recursive initialization terminates only the process");
    require(!dihos_user_spawn_elf(user_lifetime_limits_elf,sizeof(user_lifetime_limits_elf),&extra),"load bounded TLS destructor registry");
    service_deadline=dihos_time_monotonic_ms()+3000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==82 && !allocations,"TLS destructor exhaustion, repeated finalize and reuse");
    require(!dihos_user_spawn_elf(user_lifetime_overflow_elf,sizeof(user_lifetime_overflow_elf),&extra),"load compiler TLS destructor exhaustion");
    service_deadline=dihos_time_monotonic_ms()+3000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==155 && !allocations,"compiler TLS registration overflow terminates process and releases owned heap");
    print("PASS: C++ local static contention, guard abort/retry, recursive-init isolation, TLS destructor ordering/re-registration, explicit thread exit, bounded overflow and concurrent finalization\n");
    for(unsigned repeat=0;repeat<6;++repeat){
        require(!dihos_user_spawn_elf(user_sync_elf,sizeof(user_sync_elf),&extra),"load standard synchronization adapter");
        // Real guest frames allow the 5ms once callback to wake before the
        // next user syscall. Fast emulator polling previously hid this order.
        unsigned sync_polls=0;
        do {
            if(repeat>=3)simulated_frame_ms+=17;
            dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);
        }while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++sync_polls<10000);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=83){print(infos[0].last_log);print(" sync status ");hex(infos[0].exit_status);print(" PC ");hex(infos[0].pc);}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==83 && infos[0].thread_joins==3 && infos[0].thread_waits>3 && !allocations,"standard mutex/recursive mutex/once, scoped locks, native condition waits and resource cleanup");
    }
    print("PASS: standard synchronization completes with fast and guest-frame-paced syscall scheduling\n");
    require(!dihos_user_spawn_elf(user_sync_invalid_elf,sizeof(user_sync_invalid_elf),&extra),"load invalid standard mutex operation");
    service_deadline=dihos_time_monotonic_ms()+3000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<service_deadline);
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==156 && !allocations,"wrong-owner unlock is an isolated application abort");
    print("PASS: libc++ SACX mutex/recursive mutex/call_once, multi-lock contention, condition broadcast/timeout/relock and invalid-operation isolation\n");
    for(unsigned repeat=0;repeat<4;++repeat){
        require(!dihos_user_spawn_elf(user_stdthreads_elf,sizeof(user_stdthreads_elf),&extra),"load standard C++ thread adapter");
        unsigned polls=0;
        do {
            if(repeat&1)simulated_frame_ms+=17;
            dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);
        }while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<10000);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=84){print(infos[0].last_log);print(" stdthread status ");hex(infos[0].exit_status);print(" ESR ");hex(infos[0].esr);print(" PC ");hex(infos[0].pc);}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==84 && infos[0].thread_joins==16 && !allocations,"std::thread move-only ownership, IDs, join, sleep, TLS/key cleanup, deferred exit notification and repeated reuse");
    }
    require(!dihos_user_spawn_elf(user_keys_elf,sizeof(user_keys_elf),&extra),"load generation-checked thread key fixture");
    unsigned key_polls=0;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++key_polls<10000);
    if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=85){print(infos[0].last_log);hex(infos[0].exit_status);}
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==85 && infos[0].thread_joins==6 && !allocations,"thread key exhaustion, stale generations, isolation, reinstall limit and cleanup before join");
    const unsigned char *thread_failures[]={user_stdthreads_limit_elf,user_stdthreads_unjoined_elf,user_stdthreads_detach_elf};
    const unsigned thread_failure_bytes[]={sizeof(user_stdthreads_limit_elf),sizeof(user_stdthreads_unjoined_elf),sizeof(user_stdthreads_detach_elf)};
    const unsigned thread_failure_status[]={156,158,156};
    for(unsigned i=0;i<3;++i){
        require(!dihos_user_spawn_elf(thread_failures[i],thread_failure_bytes[i],&extra),"load invalid standard thread lifecycle");
        unsigned polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<10000);
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==thread_failure_status[i] && !allocations,"standard thread limit/unjoined/detach failure stays in process and releases all resources");
    }
    print("PASS: standard C++ threads, owned callbacks/arguments, moves, IDs, sleep/clock, TLS keys/destructors, exit notifications, generation reuse and isolated lifecycle failures\n");
    require(!dihos_user_spawn_elf(user_random_unready_elf,sizeof(user_random_unready_elf),&extra),"load unseeded random fixture");
    unsigned random_polls=0;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
    while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++random_polls<10000);
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==92 && !allocations,"unseeded entropy returns promptly without writing or leaking resources");
#ifdef USER_HAS_WEBKIT_RANDOM
    const unsigned char *random_adapters[]={user_random_bmalloc_elf,user_random_wtf_elf};
    unsigned random_adapter_sizes[]={sizeof(user_random_bmalloc_elf),sizeof(user_random_wtf_elf)};
    for(unsigned i=0;i<2;++i){
        require(!dihos_user_spawn_elf(random_adapters[i],random_adapter_sizes[i],&extra),"load unseeded actual WebKit random adapter");
        unsigned polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<10000);
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==185 && !allocations,"both WebKit random interfaces fail closed without blocking the kernel");
    }
#endif
    // Deterministic fixture seed only. This is not proof of firmware/RNDR quality.
    static boot_info rng_boot;rng_boot.version=3;rng_boot.boot_rng_flags=3;
    for(unsigned i=0;i<48;++i)rng_boot.boot_rng_seed[i]=(uint8_t)i;
    dihos_entropy_init(&rng_boot);
    for(unsigned i=0;i<48;++i)require(!rng_boot.boot_rng_seed[i],"boot entropy consumed and erased");
#ifdef USER_TEST_REQUIRE_RNDR
    dihos_entropy_info hardware_rng;dihos_entropy_status(&hardware_rng);
    require(hardware_rng.hardware && (hardware_rng.sources&DIHOS_ENTROPY_RNDR),"advertised RNDR instruction and condition flags produced a seed");
    print("PASS: actual architectural RNDR instruction path on QEMU max CPU\n");
#endif
    for(unsigned repeat=0;repeat<4;++repeat){
        require(!dihos_user_spawn_elf(user_random_elf,sizeof(user_random_elf),&extra),"load random syscall fixture");
        unsigned polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<10000);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=93){print(infos[0].last_log);hex(infos[0].exit_status);}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==93 && !allocations,"random syscall chunking, owned output validation and resource cleanup");
    }
    print("PASS: nonblocking entropy syscall, consumed test-only boot seed, protected output rejection and repeated EL0 cleanup\n");
#ifdef USER_HAS_WEBKIT_RANDOM
    for(unsigned i=0;i<4;++i){
        unsigned which=i&1;require(!dihos_user_spawn_elf(random_adapters[which],random_adapter_sizes[which],&extra),"load seeded actual WebKit random adapter");
        unsigned polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<10000);
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==94 && !allocations,"actual native bmalloc/WTF random buffers, integers and floating range with cleanup");
    }
    print("PASS: actual native bmalloc/WTF random adapters: fail-closed unseeded, seeded output and isolated cleanup\n");
#endif
    for(unsigned repeat=0;repeat<4;++repeat){
        require(!dihos_user_spawn_elf(user_vm_elf,sizeof(user_vm_elf),&extra),"load native VM fixture");
        unsigned polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<10000);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=90){print(infos[0].last_log);hex(infos[0].exit_status);hex(infos[0].pc);}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==90 && infos[0].thread_joins==2 && !allocations,"native VM aligned guarded zero/discard/protect, concurrency and repeated cleanup");
    }
    const unsigned char *vm_failures[]={user_vm_guard_elf,user_vm_decommit_elf,user_vm_readonly_elf};
    const unsigned vm_failure_bytes[]={sizeof(user_vm_guard_elf),sizeof(user_vm_decommit_elf),sizeof(user_vm_readonly_elf)};
    for(unsigned i=0;i<3;++i){
        require(!dihos_user_spawn_elf(vm_failures[i],vm_failure_bytes[i],&extra),"load native VM fault fixture");
        unsigned polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<10000);
        require(infos[0].state==DIHOS_USER_FAILED && !allocations,"VM guard/decommitted/read-only hardware faults isolated and reclaimed");
    }
    print("PASS: native VM alignment/guards, zeroing, decommit/recommit, protection, concurrent registry and isolated hardware faults\n");
    for(unsigned repeat=0;repeat<4;++repeat){
        require(!dihos_user_spawn_elf(user_calendar_elf,sizeof(user_calendar_elf),&extra),"load native UTC calendar fixture");
        unsigned calendar_polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++calendar_polls<10000);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=101)print(infos[0].last_log);
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==101 && infos[0].thread_joins==1 && !allocations,"native Gregorian calendar, JS date limits, normalization and thread-local storage");
        require(!dihos_user_spawn_elf(user_platform_elf,sizeof(user_platform_elf),&extra),"load native platform fixture");
        unsigned platform_polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++platform_polls<30000);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=97){print(infos[0].last_log);hex(infos[0].exit_status);hex(infos[0].pc);}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==97 && infos[0].thread_joins==2 && infos[0].thread_detaches==100 && infos[0].threads_exited==102 && !allocations,"native FP environment, UTC, large stacks and detached-thread cleanup");
        require(!dihos_user_spawn_elf(user_context_elf,sizeof(user_context_elf),&extra),"load owned thread context fixture");
        unsigned polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<30000);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=96){print(infos[0].last_log);hex(infos[0].exit_status);hex(infos[0].pc);}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==96 && infos[0].thread_joins==6 && !allocations,"owned stopped-thread GPR/SIMD/FP state, multiple stops, waits, stale handles and inspector exit cleanup");
    }
    print("PASS: firmware-anchored UTC, FP rounding inheritance/isolation, 1 MiB guarded stacks and 100 detached-thread reclaims\n");
#ifdef USER_HAS_FEATURE_MAIN
    const unsigned char *feature_programs[]={user_feature_c_elf,user_feature_cpp0_elf,user_feature_cpp2_elf};
    unsigned feature_sizes[]={sizeof(user_feature_c_elf),sizeof(user_feature_cpp0_elf),sizeof(user_feature_cpp2_elf)};
    for(unsigned which=0;which<3;++which){
        require(!dihos_user_spawn_elf(feature_programs[which],feature_sizes[which],&extra),"load real C/C++ main bridge fixture");
        unsigned polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<10000);
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==98+which && !allocations,"freestanding C and C++ main entry points really execute");
    }
    print("PASS: actual C, C++ main() and C++ main(argc,argv) bridge entry points execute in EL0\n");
#endif
    require(!dihos_user_spawn_elf(user_context_cancel_elf,sizeof(user_context_cancel_elf),&extra),"load stopped-thread cancellation fixture");
    for(unsigned polls=0;polls<200;++polls)dihos_user_runtime_poll();
    dihos_user_runtime_snapshot(infos,4);
    require(infos[0].state==DIHOS_USER_WAITING && infos[0].threads_live==2 && infos[0].threads_held==1 && infos[0].inspection_owner && infos[0].last_log[0]=='S',"cancellation fixture reached an inspector sleep with held spinner");
    require(!dihos_user_runtime_stop(extra) && !allocations,"cancellation releases held target and inspector pages");
    print("PASS: real stopped-thread contexts, owned inspection, wait/notification recovery, inspector exit and cancellation\n");
#ifdef USER_HAS_WEBKIT_SYNC
    dihos_time_init_utc(0,0);
    require(!dihos_user_spawn_elf(user_webkit_sync_elf,sizeof(user_webkit_sync_elf),&extra),"load missing-UTC actual WTF clock fixture");
    unsigned clock_polls=0;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
    while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++clock_polls<10000);
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==193 && !allocations,"actual WTF clock fails closed without a real UTC anchor");
    dihos_time_init_utc(1700000000,1);
    for(unsigned repeat=0;repeat<4;++repeat){
        require(!dihos_user_spawn_elf(user_webkit_sync_elf,sizeof(user_webkit_sync_elf),&extra),"load actual WTF synchronization fixture");
        unsigned polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<20000);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=95){print(infos[0].last_log);hex(infos[0].exit_status);hex(infos[0].pc);}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==95 && infos[0].thread_joins==6 && !allocations,"actual WTF mutex, timed condition, broadcast, thread keys and repeated cleanup");
    }
    require(!dihos_user_spawn_elf(user_webkit_invalid_elf,sizeof(user_webkit_invalid_elf),&extra),"load invalid WTF unlock fixture");
    unsigned sync_polls=0;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
    while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++sync_polls<10000);
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==187 && !allocations,"invalid WTF unlock terminates only the application");
    require(!dihos_user_spawn_elf(user_webkit_invalid_key_elf,sizeof(user_webkit_invalid_key_elf),&extra),"load stale WTF key fixture");
    sync_polls=0;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
    while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++sync_polls<10000);
    require(infos[0].state==DIHOS_USER_FAILED && !allocations,"stale WTF key traps only the application");
    print("PASS: actual native WTF synchronization, TLS destructors, timed waits, six joins and invalid-unlock containment\n");
#endif
#ifdef USER_HAS_WEBKIT_VM
    for(unsigned repeat=0;repeat<4;++repeat){
        require(!dihos_user_spawn_elf(user_webkit_vm_elf,sizeof(user_webkit_vm_elf),&extra),"load actual WebKit VM adapter fixture");
        unsigned polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<10000);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=91){print(infos[0].last_log);hex(infos[0].exit_status);hex(infos[0].pc);}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==91 && !allocations,"actual bmalloc/WTF adapter zero/discard/protect/alignment and cleanup");
    }
    print("PASS: actual pinned bmalloc and native WTF OSAllocator adapters execute in EL0 with repeated cleanup\n");
#endif
#ifdef USER_HAS_LIBM
    for(unsigned repeat=0;repeat<4;++repeat){
        require(!dihos_user_spawn_elf(user_math_elf,sizeof(user_math_elf),&extra),"load native math fixture");
        unsigned polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<10000);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=96){print(infos[0].last_log);hex(infos[0].exit_status);hex(infos[0].pc);}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==96 && !allocations,"native math special values, rounding, FP exceptions and repeated cleanup");
#if USER_LIBM_C_SUPPORT
        require(infos[0].last_log[0]=='C',"native C exit callbacks execute in LIFO order after returning from main");
#endif
    }
    print("PASS: pinned native math executes in EL0: arithmetic, special values, rounding, FP exceptions and repeated cleanup\n");
#endif
#ifdef USER_HAS_JSC_SUPPORT
    for(unsigned repeat=0;repeat<4;++repeat){
        require(!dihos_user_spawn_elf(user_jsc_support_elf,sizeof(user_jsc_support_elf),&extra),"load native LLVM arithmetic and libc++ hash fixture");
        unsigned polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<20000);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=98){print(infos[0].last_log);hex(infos[0].exit_status);hex(infos[0].pc);}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==98 && !allocations,"native LLVM integer oracle, conversions, libc++ hash containers and repeated cleanup");
    }
    print("PASS: pinned LLVM 128-bit arithmetic against Python oracle, conversions, actual libc++ hashing and repeated cleanup\n");
#endif
#ifdef USER_HAS_STDIO
    for(unsigned repeat=0;repeat<4;++repeat){
        require(!dihos_user_spawn_elf(user_stdio_elf,sizeof(user_stdio_elf),&extra),"load native C formatting and parsing fixture");
        unsigned polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<20000);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=99){print(infos[0].last_log);hex(infos[0].exit_status);hex(infos[0].pc);}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==99 && !allocations,"native C formatting/parsing, denied files, stream states, TLS errors, concurrent output and repeated cleanup");
    }
    print("PASS: native C formatting, parsing, denied files, stream states, thread-local errors, concurrent output and repeated cleanup\n");
#endif
#ifdef USER_HAS_RTTI
    for(unsigned repeat=0;repeat<4;++repeat){
        require(!dihos_user_spawn_elf(user_rtti_elf,sizeof(user_rtti_elf),&extra),"load native RTTI fixture");
        unsigned polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
        while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<10000);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=88){print(infos[0].last_log);hex(infos[0].exit_status);hex(infos[0].pc);}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==88 && !allocations,"native RTTI diamond/cross/rejected casts and type identity with repeated cleanup");
    }
    print("PASS: native libc++abi RTTI in EL0: virtual diamond, cross-cast, null/private rejection, type identity and repeated cleanup\n");
#endif
    require(!dihos_user_spawn_elf(user_stdthreads_elf,sizeof(user_stdthreads_elf),&extra),"load standard thread allocation-failure case");
    fail_after=0;unsigned oom_polls=0;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++oom_polls<1000);
    fail_after=-1;
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==126 && !allocations,"standard thread startup allocation failure releases image/stack/tables and aborts only its process");
#ifdef USER_BROWSER_BUDGET_TEST
    for(unsigned policy=0;policy<3;++policy){
        uint64_t pages=(sizeof(user_browser_budget_elf)+4095)/4096;
        void *source=pmem_alloc_pages(pages);require(source!=0,"budget probe source");
        memcpy(source,user_browser_budget_elf,sizeof(user_browser_budget_elf));
        int rc=policy==2?dihos_user_spawn_owned_browser_elf(source,sizeof(user_browser_budget_elf),pages,&extra):
            dihos_user_spawn_owned_elf_with_grants(source,sizeof(user_browser_budget_elf),pages,
                policy==1?DIHOS_PROCESS_CAP_NETWORK|DIHOS_PROCESS_CAP_COMPOSITOR_SURFACE:0,&extra);
        require(!rc,"queue budget probe");unsigned polls=0;
        do{dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++polls<20000);
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==(policy==2?87:88) && !allocations,"trusted-only browser budget and cleanup");
    }
    print("PASS: trusted browser gets 768 MiB; ordinary and network/window-granted apps retain 256 MiB; cleanup complete\n");
#endif
    dihos_user_elf_plan large_plan;
    require(!dihos_user_elf_headers(user_large_elf,sizeof(user_large_elf),&large_plan) && large_plan.bytes>24*1024*1024 && large_plan.relocation_count>256,"large bounded metadata without materializing relocation array");
    unsigned large_pages=(sizeof(user_large_elf)+4095)/4096;
    uint8_t *large_source=pmem_alloc_pages(large_pages);require(large_source!=0,"large owned source");
    memcpy(large_source,user_large_elf,sizeof(user_large_elf));
    require(!dihos_user_spawn_owned_elf(large_source,sizeof(user_large_elf),large_pages,&extra),"queue large owned ELF");
    unsigned large_polls=0;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);++large_polls;
        if(infos[0].state==DIHOS_USER_LOADING)require(!infos[0].slices,"no user entry before full image admission");
    }while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && large_polls<12000);
    if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=86){print(infos[0].last_log);print(" large status ");hex(infos[0].exit_status);print(" phase/error ");hex(infos[0].admission_step);hex((uint64_t)infos[0].admission_result);hex(infos[0].pc);}
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==86 && infos[0].memory_peak>=16*1024*1024 && large_polls>1000 && !allocations,"20 MiB file, 24 MiB image, ordered relocations, BSS, 8 MiB allocations and complete cleanup");
    for(unsigned phase=1;phase<=7;++phase){
        large_source=pmem_alloc_pages(large_pages);require(large_source!=0,"cancellation source");memcpy(large_source,user_large_elf,sizeof(user_large_elf));
        require(!dihos_user_spawn_owned_elf(large_source,sizeof(user_large_elf),large_pages,&extra),"queue image cancellation");
        unsigned polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state==DIHOS_USER_LOADING && infos[0].admission_step!=20+phase && ++polls<3000);
        if(infos[0].admission_step!=20+phase){print(" cancel phase/state/step/error ");hex(phase);hex(infos[0].state);hex(infos[0].admission_step);hex((uint64_t)infos[0].admission_result);}
        require(infos[0].admission_step==20+phase,"reached each incremental image phase");
        require(!dihos_user_runtime_stop(extra) && !allocations,"cancel each image phase reclaims staging/image/stack/tables");
    }
    unsigned fault_pages=(sizeof(user_large_fault_elf)+4095)/4096;
    large_source=pmem_alloc_pages(fault_pages);require(large_source!=0,"large fault source");memcpy(large_source,user_large_fault_elf,sizeof(user_large_fault_elf));
    require(!dihos_user_spawn_owned_elf(large_source,sizeof(user_large_fault_elf),fault_pages,&extra),"queue large RO fault");
    large_polls=0;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++large_polls<3000);
    require(infos[0].state==DIHOS_USER_FAILED && infos[0].esr && !allocations,"large readonly segment hardware fault stays isolated");
    large_source=pmem_alloc_pages(large_pages);require(large_source!=0,"malformed large source");memcpy(large_source,user_large_elf,sizeof(user_large_elf));
    uint64_t bad_relocation=large_plan.relocation_offset+(uint64_t)(large_plan.relocation_count-1)*24+8;
    large_source[bad_relocation]=2;large_source[bad_relocation+1]=4; // R_AARCH64_ABS64, not RELATIVE.
    require(!dihos_user_spawn_owned_elf(large_source,sizeof(user_large_elf),large_pages,&extra),"defer large relocation validation");
    large_polls=0;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state==DIHOS_USER_LOADING && ++large_polls<100);
    require(infos[0].state==DIHOS_USER_FAILED && infos[0].admission_result<0 && !infos[0].slices && !allocations,"late invalid relocation rejected before user entry with staging cleanup");
    for(unsigned failure=0;failure<5;++failure){
        large_source=pmem_alloc_pages(large_pages);require(large_source!=0,"OOM admission source");memcpy(large_source,user_large_elf,sizeof(user_large_elf));
        require(!dihos_user_spawn_owned_elf(large_source,sizeof(user_large_elf),large_pages,&extra),"queue failing large image allocation");
        fail_after=(int)failure;large_polls=0;
        do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(infos[0].state==DIHOS_USER_LOADING && ++large_polls<3000);
        fail_after=-1;require(infos[0].state==DIHOS_USER_FAILED && !allocations,"large image backing/stack/table allocation failures reclaim all resources");
    }
    large_source=pmem_alloc_pages(large_pages);require(large_source!=0,"large heap cancellation source");memcpy(large_source,user_large_elf,sizeof(user_large_elf));
    require(!dihos_user_spawn_owned_elf(large_source,sizeof(user_large_elf),large_pages,&extra),"queue large heap cancellation");
    large_polls=0;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}while(!infos[0].memory_committed && infos[0].state!=DIHOS_USER_FAILED && ++large_polls<5000);
    require(infos[0].memory_committed>=65536 && !dihos_user_runtime_stop(extra) && !allocations,"cancel active large reservation during incremental commit");
    print("PASS: incremental 20 MiB ELF, indexed relocations, large zeroed BSS/heap, 8 MiB malloc, RO protection and cancellation in every admission phase\n");
    print("PASS: compiled ELF programs: initialized data/BSS, owned bytes, preemption, sleep, fault isolation and allocation failure cleanup\n");
    print("PASS: fully mapped firmware admission with owned backing aliases\n");
    print("PASS: round-robin tasks, nonblocking sleep, fault isolation, loop cancellation, exhausted slots, allocation failures and 100 create/close cycles\n");
#ifdef USER_HAS_JSC
    dihos_entropy_info entropy;dihos_entropy_status(&entropy);require(entropy.ready,"test-owned entropy ready for native JSC");
    print("Native JSC ELF bytes ");hex(sizeof(user_jsc_elf));print("\n");
    uint64_t engine_pages=(sizeof(user_jsc_elf)+4095)/4096;
    void *engine_source=pmem_alloc_pages(engine_pages);require(engine_source!=0,"allocate owned native JSC source");
    memcpy(engine_source,user_jsc_elf,sizeof(user_jsc_elf));
    int engine_queue=dihos_user_spawn_owned_elf(engine_source,sizeof(user_jsc_elf),engine_pages,&extra);
    if(engine_queue){pmem_free_pages(engine_source,engine_pages);print("JSC admission error ");hex((uint64_t)(int64_t)engine_queue);print("\n");}
    require(!engine_queue,"queue actual native JavaScriptCore executable");
    unsigned engine_polls=0;uint64_t engine_start=dihos_time_monotonic_ms();char last_stage[SACX_USER_BUFFER_MAX+1]={0};
    int engine_execution_started=0;
    uint64_t engine_reserved_peak=0,engine_committed_peak=0;
    do {
#ifdef USER_JSC_FRAME_PACED
        uint64_t frame_started=dihos_time_monotonic_ms();
        require(dihos_user_runtime_service()<=64,"desktop service operation cap");
#else
        dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);
#endif
        dihos_user_runtime_snapshot(infos,4);
        if(infos[0].memory_reserved>engine_reserved_peak)engine_reserved_peak=infos[0].memory_reserved;
        if(infos[0].memory_peak>engine_committed_peak)engine_committed_peak=infos[0].memory_peak;
        if(!engine_execution_started && infos[0].state!=DIHOS_USER_LOADING){
            engine_execution_started=1;engine_start=dihos_time_monotonic_ms();
        }
        int changed=0;for(unsigned i=0;i<sizeof(last_stage);++i){if(last_stage[i]!=infos[0].last_log[i]){changed=1;break;}if(!last_stage[i])break;}
        if(changed){memcpy(last_stage,infos[0].last_log,sizeof(last_stage));print("JSC: ");print(last_stage);print("\n");}
#ifdef USER_JSC_FRAME_PACED
        if(infos[0].state==DIHOS_USER_READY || infos[0].state==DIHOS_USER_WAITING)
            while(dihos_time_monotonic_ms()-frame_started<16)__asm__ volatile("yield");
#endif
    }
    while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && ++engine_polls<5000000 && dihos_time_monotonic_ms()-engine_start<60000);
    if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=87){print(infos[0].last_log);print(" JSC status/state/calls/slices/preemptions/code ");hex(infos[0].exit_status);hex(infos[0].state);hex(infos[0].sacx_calls);hex(infos[0].slices);hex(infos[0].preemptions);hex(infos[0].code_va);print(" PC ");hex(infos[0].pc);print(" ESR ");hex(infos[0].esr);print(" FAR ");hex(infos[0].far);}
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==87 && !allocations,"actual native JSC context, evaluation result, release and process cleanup");
    print("JSC peak reserved/committed bytes: ");hex(engine_reserved_peak);hex(engine_committed_peak);print("\n");
    print("PASS: actual native JavaScriptCore creates a context, evaluates JavaScript to 4950, releases the context and reclaims process memory\n");
#ifdef USER_JSC_FRAME_PACED
    print("JSC execution slices/calls/waits/wakes: ");hex(infos[0].slices);hex(infos[0].sacx_calls);hex(infos[0].thread_waits);hex(infos[0].thread_wakes);print("\n");
    print("PASS: actual JavaScriptCore full suite and GC complete with bounded desktop service and 16 ms execution frames\n");
#endif
#ifdef USER_HAS_JSC_SPIN
    engine_pages=(sizeof(user_jsc_spin_elf)+4095)/4096;
    engine_source=pmem_alloc_pages(engine_pages);require(engine_source!=0,"allocate busy engine source");
    memcpy(engine_source,user_jsc_spin_elf,sizeof(user_jsc_spin_elf));
    engine_queue=dihos_user_spawn_owned_elf(engine_source,sizeof(user_jsc_spin_elf),engine_pages,&extra);
    if(engine_queue)pmem_free_pages(engine_source,engine_pages);
    require(!engine_queue,"queue infinite JavaScript fixture");
    engine_polls=0;engine_start=dihos_time_monotonic_ms();
    const char busy_stage[]="JSC native: evaluating script";int busy_stage_seen=0;
    uint64_t busy_stage_preemptions=0;
    do {
        dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);
        int matches=1;
        for(unsigned i=0;i<sizeof(busy_stage);++i)if(infos[0].last_log[i]!=busy_stage[i])matches=0;
        if(matches && !busy_stage_seen){busy_stage_seen=1;busy_stage_preemptions=infos[0].preemptions;}
    }
    while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED &&
          (!busy_stage_seen || infos[0].preemptions<busy_stage_preemptions+16) &&
          ++engine_polls<5000000 && dihos_time_monotonic_ms()-engine_start<60000);
    if(!busy_stage_seen || infos[0].preemptions<busy_stage_preemptions+16){
        print("JSC spin: ");print(infos[0].last_log);print(" state/preemptions/PC/ESR ");
        hex(infos[0].state);hex(infos[0].preemptions);hex(infos[0].pc);hex(infos[0].esr);print("\n");
    }
    require(busy_stage_seen && infos[0].preemptions>=busy_stage_preemptions+16,
            "infinite JavaScript reaches interpreter and is timer preempted");
    uint64_t busy_calls=infos[0].sacx_calls,busy_preemptions=infos[0].preemptions;
    uint64_t busy_deadline=dihos_time_monotonic_ms()+5000;
    do {
        dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);
        /* Timer delivery can interrupt interpreter startup before the loop
         * has settled. Measure eight actual API-free preemptions, rather
         * than assuming every scheduler poll is already one such slice. */
        if(infos[0].sacx_calls!=busy_calls){busy_calls=infos[0].sacx_calls;busy_preemptions=infos[0].preemptions;}
    }while(infos[0].state==DIHOS_USER_READY && infos[0].preemptions<busy_preemptions+8 && dihos_time_monotonic_ms()<busy_deadline);
    require(infos[0].state==DIHOS_USER_READY && infos[0].preemptions>=busy_preemptions+8 && infos[0].sacx_calls==busy_calls,
            "busy JavaScript yields to kernel without application API calls");
    require(!dihos_user_runtime_stop(extra) && !allocations,"cancel infinite JavaScript and reclaim owned memory");
    print("PASS: infinite native JavaScript timer preemption, kernel progress without engine syscalls, cancellation and complete cleanup\n");
#endif
#endif
#ifdef USER_HAS_WEBCORE_LOOP
    uint64_t loop_pages=(sizeof(user_webcore_loop_elf)+4095)/4096;
    void *loop_source=pmem_alloc_pages(loop_pages);require(loop_source!=0,"allocate event-loop ELF source");
    memcpy(loop_source,user_webcore_loop_elf,sizeof(user_webcore_loop_elf));
    int loop_queue=dihos_user_spawn_owned_elf(loop_source,sizeof(user_webcore_loop_elf),loop_pages,&extra);
    if(loop_queue)pmem_free_pages(loop_source,loop_pages);
    require(!loop_queue,"queue native event-loop fixture");
    uint64_t loop_deadline=dihos_time_monotonic_ms()+60000;
    char loop_stage[SACX_USER_BUFFER_MAX+1]={0};
    do {
        uint64_t frame=dihos_time_monotonic_ms();
        require(dihos_user_runtime_service()<=64,"event loop bounded desktop service");
        dihos_user_runtime_snapshot(infos,4);
        int changed=0;for(unsigned i=0;i<sizeof(loop_stage);++i){if(loop_stage[i]!=infos[0].last_log[i]){changed=1;break;}if(!loop_stage[i])break;}
        if(changed){memcpy(loop_stage,infos[0].last_log,sizeof(loop_stage));print("WebCore: ");print(loop_stage);print("\n");}
        if(infos[0].state==DIHOS_USER_READY || infos[0].state==DIHOS_USER_WAITING)
            while(dihos_time_monotonic_ms()-frame<16)__asm__ volatile("yield");
    }while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<loop_deadline);
    if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=103){print("Event loop state/status/PC/ESR ");hex(infos[0].state);hex(infos[0].exit_status);hex(infos[0].pc);hex(infos[0].esr);print("\n");}
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==103 && infos[0].threads_created>=2 && !allocations,"native event-loop fixture exits and reclaims all process resources");
    print("PASS: native WebKit RunLoop, bounded dispatch, cross-thread cancellation, timers and protected JavaScript callbacks under desktop pacing\n");
#endif
#ifdef USER_HAS_HTTP
    require(!dihos_user_spawn_elf(user_http_elf,sizeof(user_http_elf),&extra),"queue ungranted HTTP fixture");
    for(unsigned i=0;i<10000;++i){dihos_user_runtime_service();dihos_user_runtime_snapshot(infos,4);if(infos[0].state==DIHOS_USER_DONE || infos[0].state==DIHOS_USER_FAILED)break;}
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==105 && !allocations,"network denied by ordinary ELF launch");
    for(unsigned run=0;run<2;++run){
        uint64_t pages=(sizeof(user_http_elf)+4095)/4096;void *source=pmem_alloc_pages(pages);require(source!=0,"HTTP fixture source");
        memcpy(source,user_http_elf,sizeof(user_http_elf));
        require(!dihos_user_spawn_owned_elf_with_network(source,sizeof(user_http_elf),pages,1,&extra),"explicit network-granted ELF");
        uint64_t deadline=dihos_time_monotonic_ms()+10000;
        do {
            dihos_user_runtime_service();dihos_user_runtime_snapshot(infos,4);
            if(!run)user_http_transport_step();
            else if(G_user_http[0].owner){require(!dihos_user_runtime_stop(extra),"cancel process with queued HTTP");break;}
        }while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<deadline);
        dihos_user_runtime_snapshot(infos,4);
        if(!run && (infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=104)){print(infos[0].last_log);hex(infos[0].exit_status);}
        require(!allocations && (run?infos[0].state==DIHOS_USER_STOPPED:infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==104),"HTTP completion/cancellation cleanup");
        for(unsigned i=0;i<USER_HTTP_SLOTS;++i)require(!G_user_http[i].owner && !G_user_http[i].request.used,"no orphaned HTTP slots");
    }
    print("PASS: EL0 HTTP grant denial/explicit grant, queued transport, protected output rejection, reads, stale handles and process cancellation cleanup (modeled transport)\n");
#endif
#ifdef USER_HAS_RESOURCE_LOOP
    for(unsigned granted=0;granted<2;++granted){
        uint64_t pages=(sizeof(user_resource_loop_elf)+4095)/4096;
        void *source=pmem_alloc_pages(pages);require(source!=0,"allocate native resource ELF source");
        memcpy(source,user_resource_loop_elf,sizeof(user_resource_loop_elf));
        require(!dihos_user_spawn_owned_elf_with_network(source,sizeof(user_resource_loop_elf),pages,granted,&extra),"queue native resource client");
        uint64_t deadline=dihos_time_monotonic_ms()+60000;char stage[SACX_USER_BUFFER_MAX+1]={0};int executing=0;
        do {
            uint64_t frame=dihos_time_monotonic_ms();require(dihos_user_runtime_service()<=64,"resource client desktop budget");
            user_http_transport_step();dihos_user_runtime_snapshot(infos,4);
            if(!executing && infos[0].state!=DIHOS_USER_LOADING){executing=1;deadline=dihos_time_monotonic_ms()+60000;}
            int changed=0;for(unsigned i=0;i<sizeof(stage);++i){if(stage[i]!=infos[0].last_log[i]){changed=1;break;}if(!stage[i])break;}
            if(changed){memcpy(stage,infos[0].last_log,sizeof(stage));print("Resource: ");print(stage);print("\n");}
            if(infos[0].state==DIHOS_USER_READY || infos[0].state==DIHOS_USER_WAITING)
                while(dihos_time_monotonic_ms()-frame<16)__asm__ volatile("yield");
        }while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<deadline);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=(granted?106:108)){
            print("Resource state/status/PC/ESR ");hex(infos[0].state);hex(infos[0].exit_status);hex(infos[0].pc);hex(infos[0].esr);print("\n");}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==(granted?106:108) && !allocations,"resource client callback contract and full process cleanup");
    }
    print("PASS: native RunLoop HTTP resource client, callback/promise order, reentrant cancellation, weak lifetime, deadlines, TLS rejection and backpressure (modeled transport)\n");
#endif
#ifdef USER_HAS_WEBCORE_RESOURCE
    for(unsigned granted=0;granted<2;++granted){
        uint64_t pages=(sizeof(user_webcore_resource_elf)+4095)/4096;
        void *source=pmem_alloc_pages(pages);require(source!=0,"allocate WebCore resource ELF source");
        memcpy(source,user_webcore_resource_elf,sizeof(user_webcore_resource_elf));
        require(!dihos_user_spawn_owned_elf_with_network(source,sizeof(user_webcore_resource_elf),pages,granted,&extra),"queue WebCore resource fixture");
        uint64_t deadline=dihos_time_monotonic_ms()+60000;char stage[SACX_USER_BUFFER_MAX+1]={0};int executing=0;
        do {
            uint64_t frame=dihos_time_monotonic_ms();require(dihos_user_runtime_service()<=64,"WebCore resource desktop budget");
            user_http_transport_step();dihos_user_runtime_snapshot(infos,4);
            if(!executing && infos[0].state!=DIHOS_USER_LOADING){executing=1;deadline=dihos_time_monotonic_ms()+60000;}
            int changed=0;for(unsigned i=0;i<sizeof(stage);++i){if(stage[i]!=infos[0].last_log[i]){changed=1;break;}if(!stage[i])break;}
            if(changed){memcpy(stage,infos[0].last_log,sizeof(stage));print("WebCore resource: ");print(stage);print("\n");}
            if(infos[0].state==DIHOS_USER_READY || infos[0].state==DIHOS_USER_WAITING)
                while(dihos_time_monotonic_ms()-frame<16)__asm__ volatile("yield");
        }while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<deadline);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=(granted?111:112)){
            print("WebCore resource state/status/PC/ESR ");hex(infos[0].state);hex(infos[0].exit_status);hex(infos[0].pc);hex(infos[0].esr);print("\n");}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==(granted?111:112) && !allocations,"WebCore resource metadata/callback contract and full cleanup");
    }
    print("PASS: actual WebCore request/response/error metadata, strict headers, manual redirects, MIME/charset, TLS rejection, cancellation and checked SACX streaming (modeled transport)\n");
#endif
#ifdef USER_HAS_CAIRO_RASTER
    {
        uint64_t pages=(sizeof(user_cairo_raster_elf)+4095)/4096;
        void *source=pmem_alloc_pages(pages);require(source!=0,"allocate Cairo ELF source");
        memcpy(source,user_cairo_raster_elf,sizeof(user_cairo_raster_elf));
        require(!dihos_user_spawn_owned_elf_with_network(source,sizeof(user_cairo_raster_elf),pages,0,&extra),"queue native Cairo fixture without network grant");
        uint64_t deadline=dihos_time_monotonic_ms()+30000;char stage[SACX_USER_BUFFER_MAX+1]={0};
        do {
            require(dihos_user_runtime_service()<=64,"Cairo desktop service budget");
            dihos_user_runtime_snapshot(infos,4);
            int changed=0;for(unsigned i=0;i<sizeof(stage);++i){if(stage[i]!=infos[0].last_log[i]){changed=1;break;}if(!stage[i])break;}
            if(changed){memcpy(stage,infos[0].last_log,sizeof(stage));print("Cairo: ");print(stage);print("\n");}
        }while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<deadline);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=113){
            print("Cairo state/status/PC/ESR/FAR/code ");hex(infos[0].state);hex(infos[0].exit_status);hex(infos[0].pc);hex(infos[0].esr);hex(infos[0].far);hex(infos[0].code_va);print("\n");}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==113 && !allocations,"native Cairo pixels, bounds, thread safety, allocation recovery and process cleanup");
        print("PASS: native Cairo/Pixman rasterization, clipping, alpha, gradients, transforms, mutex contention, jumps and allocation recovery\n");
        pages=(sizeof(user_cairo_oom_elf)+4095)/4096;
        source=pmem_alloc_pages(pages);require(source!=0,"allocate Cairo OOM ELF source");
        memcpy(source,user_cairo_oom_elf,sizeof(user_cairo_oom_elf));
        require(!dihos_user_spawn_owned_elf_with_network(source,sizeof(user_cairo_oom_elf),pages,0,&extra),"queue critical Cairo OOM fixture");
        deadline=dihos_time_monotonic_ms()+30000;
        do {
            require(dihos_user_runtime_service()<=64,"Cairo OOM desktop budget");
            dihos_user_runtime_snapshot(infos,4);
        }while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<deadline);
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==178 && !allocations,"critical Cairo clip OOM terminates app explicitly and releases all pages");
        print("PASS: native Cairo critical clip OOM reports controlled app exit; kernel survives and reclaims all pages\n");
    }
#endif
#ifdef USER_HAS_NATIVE_FONTS
    {
        uint64_t pages=(sizeof(user_fonts_elf)+4095)/4096;
        void *source=pmem_alloc_pages(pages);require(source!=0,"allocate native fonts ELF source");
        memcpy(source,user_fonts_elf,sizeof(user_fonts_elf));
        require(!dihos_user_spawn_owned_elf_with_network(source,sizeof(user_fonts_elf),pages,0,&extra),"queue native fonts without network grant");
        uint64_t deadline=dihos_time_monotonic_ms()+30000;char stage[SACX_USER_BUFFER_MAX+1]={0};
        unsigned selection_seen=0,painting_seen=0;
        do {
            require(dihos_user_runtime_service()<=64,"native fonts desktop service budget");
            dihos_user_runtime_snapshot(infos,4);
            int changed=0;for(unsigned i=0;i<sizeof(stage);++i){if(stage[i]!=infos[0].last_log[i]){changed=1;break;}if(!stage[i])break;}
            if(changed){memcpy(stage,infos[0].last_log,sizeof(stage));print("Fonts: ");print(stage);print("\n");}
            if(text_prefix(stage,"Native fonts: real Fontconfig"))selection_seen=1;
            if(text_prefix(stage,"Native fonts: shaped Unicode"))painting_seen=1;
        }while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<deadline);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=114){
            print("Fonts state/status/PC/ESR/FAR/code ");hex(infos[0].state);hex(infos[0].exit_status);hex(infos[0].pc);hex(infos[0].esr);hex(infos[0].far);hex(infos[0].code_va);print("\n");}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==114 && !allocations,"native fonts shape, rasterize, join workers and release all process pages");
#ifdef USER_FONTS_SELECTION
        require(selection_seen,"Fontconfig selection fixture actually executed");
#endif
#ifdef USER_FONTS_PAINT
        require(painting_seen,"Cairo shaped-text painting actually executed");
#endif
        print("PASS: native FreeType/HarfBuzz memory fonts, family metrics, Unicode clusters, ligatures, combining marks, zoom, malformed input and concurrent rasterization\n");
    }
#endif
#ifdef USER_HAS_NATIVE_PAGE
    {
#ifdef USER_NATIVE_PAGE_WINDOW
        const dihos_user_window_backend backend={native_window_create,native_window_present,native_window_destroy};
        require(!dihos_user_window_set_backend(&backend),"install owned-window test compositor");
#ifdef USER_NATIVE_PAGE_NAVIGATION
        const uint64_t page_grant=DIHOS_PROCESS_CAP_COMPOSITOR_SURFACE|DIHOS_PROCESS_CAP_NETWORK;
#ifdef USER_NATIVE_PAGE_EMBEDDED
        const uint64_t page_exit=123;
#else
        const uint64_t page_exit=122;
#endif
        nav_active=1;
#else
        const uint64_t page_grant=DIHOS_PROCESS_CAP_COMPOSITOR_SURFACE;
        const uint64_t page_exit=120;
#endif
#else
        const uint64_t page_grant=0,page_exit=115;
#endif
        uint64_t pages=(sizeof(user_native_page_elf)+4095)/4096;
        void *source=pmem_alloc_pages(pages);require(source!=0,"allocate native Page ELF source");
        memcpy(source,user_native_page_elf,sizeof(user_native_page_elf));
        require(!dihos_user_spawn_owned_elf_with_grants(source,sizeof(user_native_page_elf),pages,page_grant,&extra),"queue local Page without network grant");
#ifdef USER_IMAGE_TESTS
        require(!dihos_user_runtime_set_interactive(extra,4000),"bounded renderer CPU budget");
        require(dihos_user_runtime_set_interactive(extra,5001)<0,"reject excessive CPU quantum");
#endif
#ifdef USER_NATIVE_PAGE_EMBEDDED
        require(!dihos_browser_open(1,640,480,&browser_token),"create existing-UI browser test session");
        require(!dihos_browser_bind(1,browser_token,((uint64_t)extra.generation<<8)|(extra.slot+1u)),"bind only the owned renderer");
        browser_send(SACX_BROWSER_PREFS,0,8,100,1|(2<<8),0);
        browser_send(SACX_BROWSER_NAVIGATE,0,0,0,0,NAVIGATION_ORIGIN);
#endif
        uint64_t deadline=dihos_time_monotonic_ms()+120000;char stage[SACX_USER_BUFFER_MAX+1]={0};unsigned painted=0;
        do {
            require(dihos_user_runtime_service()<=64,"native Page desktop service budget");
#ifdef USER_NATIVE_PAGE_NAVIGATION
            user_http_transport_step();
#ifdef USER_NATIVE_PAGE_EMBEDDED
            if(last_window_pixels)embedded_browser_present(last_window_pixels);
#endif
#endif
            dihos_user_runtime_snapshot(infos,4);
            int changed=0;for(unsigned i=0;i<sizeof(stage);++i){if(stage[i]!=infos[0].last_log[i]){changed=1;break;}if(!stage[i])break;}
            if(changed){memcpy(stage,infos[0].last_log,sizeof(stage));print("Page: ");print(stage);print("\n");}
            if(text_prefix(stage,"SACX native Page: local HTML, DOM JavaScript, layout and offscreen painting OK"))painted=1;
#ifdef USER_NATIVE_PAGE_WINDOW
            if(text_prefix(stage,"SACX native Page window: checked frames, input JavaScript and close OK"))painted=1;
#endif
#ifdef USER_NATIVE_PAGE_NAVIGATION
            if(text_prefix(stage,"SACX native navigation: window closed and requests canceled"))painted=1;
#ifdef USER_NATIVE_PAGE_EMBEDDED
            if(window_frames && browser_stage==17)painted=1;
#endif
#endif
        }while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<deadline);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=page_exit){
            print("Page state/status/PC/ESR/FAR/code ");hex(infos[0].state);hex(infos[0].exit_status);hex(infos[0].pc);hex(infos[0].esr);hex(infos[0].far);hex(infos[0].code_va);print("\n");}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==page_exit && painted && !allocations,"native Page verifies DOM, JS, layout and pixels then releases owned pages");
#ifdef USER_NATIVE_PAGE_WINDOW
#ifdef USER_NATIVE_PAGE_NAVIGATION
#ifdef USER_NATIVE_PAGE_EMBEDDED
        require(nav_redirects==1 && nav_styles && nav_scripts && nav_next>=2 && nav_typed && nav_form && browser_stage==17 && nav_stalled && window_closed==3,"existing UI commands, resize, typed navigation, form cancellation, history and close cleanup");
        require(infos[0].state==DIHOS_USER_DONE,"CSS transform and will-change paint in software without accelerated layer exit");
        print("PASS: local WebCore error page paints, retains URL and reason, and Reload retries successfully\n");
        print("PASS: embedded WebCore uses the existing-UI command bridge, resizes, navigates, edits forms, preserves DOM cancellation, traverses history and closes cleanly\n");
        print("PASS: visible wheel scrolling, fixed-position pixels, responsive CSS and JavaScript resize events beyond 1024 pixels\n");
#else
        require(nav_redirects==1 && nav_styles && nav_scripts && nav_next>=2 && nav_typed && nav_form && nav_clicked && nav_green==9 && nav_stalled && window_closed==1,"redirect, external CSS/JS, typed URL, page keys, history, Stop, Reload, pixels and window cleanup");
        nav_active=0;
        print("PASS: native WebCore Frame navigation, redirects, external CSS/JavaScript, links, Stop on stalled load, Reload, pixels and cancellation cleanup (modeled HTTP)\n");
        print("PASS: native browser typed address, DOM keyboard pixels, form edits/cancellation, Back and Forward history (modeled HTTP)\n");
#endif
#else
        require(window_frames==2 && window_closed==1,"two checked window frames and clean close");
        print("PASS: native WebCore window syscall frames, queued pointer click, JavaScript CSS repaint, close and full cleanup\n");
#endif
#else
        print("PASS: native WebCore Page, HTML parsing, JavaScript DOM/style mutation, layout, memory fonts and guarded offscreen painting\n");
#endif
    }
#endif
#ifdef USER_HAS_NATIVE_VIDEO
    {
        uint64_t bytes=sizeof(user_native_video_elf),pages=(bytes+4095)/4096;
        void* source=pmem_alloc_pages(pages);require(source!=0,"allocate native video ELF source");
        memcpy(source,user_native_video_elf,bytes);
        require(!dihos_user_spawn_owned_elf_with_network(source,bytes,pages,0,&extra),"queue native video without network or window grant");
        uint64_t deadline=dihos_time_monotonic_ms()+60000;
        do {
            require(dihos_user_runtime_service()<=64,"native video desktop service budget");
            dihos_user_runtime_snapshot(infos,4);
        }while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<deadline);
        print("Video: ");print(infos[0].last_log);print("\n");
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=119){
            print("Video state/status/PC/ESR/FAR/code ");hex(infos[0].state);hex(infos[0].exit_status);hex(infos[0].pc);hex(infos[0].esr);hex(infos[0].far);hex(infos[0].code_va);print("\n");
        }
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==119 && !allocations,"native video decoding and cleanup release all process pages");
        print("PASS: native EL0 MP4/H.264 decoder, BGRA pixels, timestamps, cancellation, malformed input and process cleanup\n");
    }
#endif
#ifdef USER_HAS_NATIVE_VIDEO_PAGE
    {
        uint64_t bytes=sizeof(user_native_video_page_elf),pages=(bytes+4095)/4096;
        void* source=pmem_alloc_pages(pages);require(source!=0,"allocate video Page ELF source");
        memcpy(source,user_native_video_page_elf,bytes);
        require(!dihos_user_spawn_owned_elf_with_grants(source,bytes,pages,0,&extra),"queue offline video Page without network or window grants");
        require(!dihos_user_runtime_set_interactive(extra,4000),"bounded video Page CPU budget");
        uint64_t deadline=dihos_time_monotonic_ms()+90000;char stage[SACX_USER_BUFFER_MAX+1]={0};
        do {
            require(dihos_user_runtime_service()<=64,"video Page desktop service budget");
            dihos_user_runtime_snapshot(infos,4);
            int changed=0;for(unsigned i=0;i<sizeof(stage);++i){if(stage[i]!=infos[0].last_log[i]){changed=1;break;}if(!stage[i])break;}
            if(changed){memcpy(stage,infos[0].last_log,sizeof(stage));print("Video Page: ");print(stage);print("\n");}
        }while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<deadline);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=124){
            print("Video Page state/status/PC/ESR/FAR/code ");hex(infos[0].state);hex(infos[0].exit_status);hex(infos[0].pc);hex(infos[0].esr);hex(infos[0].far);hex(infos[0].code_va);print("\n");
        }
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==124 && !allocations,"HTML video playback and cleanup release all process pages");
        print("PASS: native WebCore HTML video, MP4/H.264 pixels, seek, timed muted playback, ended events and process cleanup\n");
    }
#endif
#ifdef USER_HAS_OPENSSL
    for(unsigned crypto_case=0;crypto_case<2;++crypto_case){
        dihos_entropy_test_reset();
        if(!crypto_case)dihos_entropy_init(0);
        else {rng_boot.boot_rng_flags=3;for(unsigned i=0;i<48;++i)rng_boot.boot_rng_seed[i]=(uint8_t)i;dihos_entropy_init(&rng_boot);}
        const unsigned char *crypto_elf=crypto_case?user_openssl_elf:user_openssl_unready_elf;
        uint64_t crypto_bytes=crypto_case?sizeof(user_openssl_elf):sizeof(user_openssl_unready_elf);
        uint64_t crypto_exit=crypto_case?116:117;
        uint64_t pages=(crypto_bytes+4095)/4096;
        void *source=pmem_alloc_pages(pages);require(source!=0,"allocate native crypto ELF source");
        memcpy(source,crypto_elf,crypto_bytes);
        require(!dihos_user_spawn_owned_elf_with_network(source,crypto_bytes,pages,0,&extra),"queue native crypto without network grant");
        uint64_t deadline=dihos_time_monotonic_ms()+60000;char stage[SACX_USER_BUFFER_MAX+1]={0};
        do {
            require(dihos_user_runtime_service()<=64,"native crypto desktop service budget");
            dihos_user_runtime_snapshot(infos,4);
            int changed=0;for(unsigned i=0;i<sizeof(stage);++i){if(stage[i]!=infos[0].last_log[i]){changed=1;break;}if(!stage[i])break;}
            if(changed){memcpy(stage,infos[0].last_log,sizeof(stage));print("Crypto: ");print(stage);print("\n");}
        }while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<deadline);
        if(infos[0].state!=DIHOS_USER_DONE || infos[0].exit_status!=crypto_exit){
            print("Crypto state/status/PC/ESR/FAR/code ");hex(infos[0].state);hex(infos[0].exit_status);hex(infos[0].pc);hex(infos[0].esr);hex(infos[0].far);hex(infos[0].code_va);print("\n");}
        require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==crypto_exit && !allocations,"native OpenSSL known answers and thread cleanup release all process pages");
        if(!crypto_case)print("PASS: native OpenSSL unavailable entropy fails closed and releases all process pages\n");
        else print("PASS: native OpenSSL vectors, authentication rejection, entropy, once, TLS and concurrent cleanup\n");
    }
#endif
    require(!dihos_user_runtime_service(),"idle desktop service returns without user work");
    require(!dihos_user_spawn_elf(user_sacx_elf,sizeof(user_sacx_elf),&extra),"desktop service syscall fixture");
    unsigned service_max=0;
    for(unsigned i=0;i<10000;++i){
        unsigned steps=dihos_user_runtime_service();require(steps<=64,"bounded desktop service steps");
        if(steps>service_max)service_max=steps;
        dihos_user_runtime_snapshot(infos,4);
        if(infos[0].state==DIHOS_USER_DONE || infos[0].state==DIHOS_USER_FAILED)break;
    }
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==73 && service_max>1 && !allocations,
            "desktop batches real short syscalls and respects sleep/yield/log/exit");
    require(!dihos_user_spawn_fixture(1,&extra),"desktop busy task");
    dihos_process_handle service_peer;
    require(!dihos_user_spawn_elf(user_sacx_elf,sizeof(user_sacx_elf),&service_peer),"desktop peer task");
    for(unsigned i=0;i<1000;++i){
        require(dihos_user_runtime_service()<=64,"fair desktop service operation cap");
        dihos_user_runtime_snapshot(infos,4);
        if(infos[1].state==DIHOS_USER_DONE || infos[1].state==DIHOS_USER_FAILED)break;
    }
    require(infos[0].preemptions>0 && infos[1].state==DIHOS_USER_DONE && infos[1].exit_status==73,
            "timer-preempted CPU loop cannot starve syscall peer");
    require(!dihos_user_runtime_stop(extra) && !allocations && !dihos_user_runtime_service(),"desktop cancellation and idle cleanup");
    print("PASS: bounded desktop service batches short syscalls, preserves yield/sleep/log boundaries, round-robin CPU fairness, cancellation and idle return\n");
    require(!dihos_user_spawn_elf(user_sparse_elf,sizeof(user_sparse_elf),&extra),"sparse ELF admission");
    uint64_t sparse_deadline=dihos_time_monotonic_ms()+5000;
    do {dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);}
    while(infos[0].state!=DIHOS_USER_DONE && infos[0].state!=DIHOS_USER_FAILED && dihos_time_monotonic_ms()<sparse_deadline);
    if(infos[0].exit_status!=101){print("sparse: ");print(infos[0].last_log);hex(infos[0].state);hex(infos[0].exit_status);}
    require(infos[0].state==DIHOS_USER_DONE && infos[0].exit_status==101 && !allocations,"GiB sparse EL0 reserve/commit/protect/decommit/release and reuse");
    const unsigned char *sparse_faults[]={user_sparse_guard_elf,user_sparse_decommit_elf,user_sparse_readonly_elf,user_sparse_nx_elf};
    const size_t sparse_sizes[]={sizeof(user_sparse_guard_elf),sizeof(user_sparse_decommit_elf),sizeof(user_sparse_readonly_elf),sizeof(user_sparse_nx_elf)};
    for(unsigned i=0;i<4;++i){
        require(!dihos_user_spawn_elf(sparse_faults[i],sparse_sizes[i],&extra),"sparse fault ELF admission");
        for(unsigned poll=0;poll<10000;++poll){dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);if(infos[0].state==DIHOS_USER_FAILED || infos[0].state==DIHOS_USER_DONE)break;}
        require(infos[0].state==DIHOS_USER_FAILED && infos[0].esr && !allocations,"sparse hardware guard/decommit/read-only faults and cleanup");
        require(((infos[0].esr>>26)&63)==(i==3?0x20u:0x24u),"sparse fault is actual data/execute permission abort");
    }
    require(!dihos_user_spawn_elf(user_sparse_spin_elf,sizeof(user_sparse_spin_elf),&extra),"resident sparse cancellation fixture");
    for(unsigned poll=0;poll<10000;++poll){dihos_user_runtime_poll();dihos_user_runtime_snapshot(infos,4);if(infos[0].preemptions>=4 || infos[0].state==DIHOS_USER_FAILED)break;}
    require(infos[0].preemptions>=4 && infos[0].memory_committed==65536 && !dihos_user_runtime_stop(extra) && !allocations,"cancel resident sparse pages and metadata");
    print("PASS: sparse GiB EL0 reservations, distant pages, physical accounting, zeroed reuse, guard/protection/decommit/NX hardware faults and cancellation cleanup\n");
    sparse_kernel_tests();
    finish(0);
}
