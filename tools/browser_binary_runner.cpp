/* Linux/AArch64 host adapter for the actual packaged Dihscover SACX binary.
 * No libc/loader replacement inside the app: its parser, layout, fonts and
 * compatibility runtime are exactly those shipped to the guest. UI/network
 * calls are deterministic mocks; this is not a Hyper-V driver/latency test. */
#include <stdint.h>
#include <stddef.h>
#include "sacx_api.h"
#include "apps/sacx_format.h"
#include "system/kfiber.h"
static long syscall6(long n,long a=0,long b=0,long c=0,long d=0,long e=0,long f=0){
    register long x0 asm("x0")=a,x1 asm("x1")=b,x2 asm("x2")=c,x3 asm("x3")=d,x4 asm("x4")=e,x5 asm("x5")=f,x8 asm("x8")=n;
    asm volatile("svc 0":"+r"(x0):"r"(x1),"r"(x2),"r"(x3),"r"(x4),"r"(x5),"r"(x8):"memory","cc");return x0;
}
extern "C" void *memcpy(void *d,const void *s,size_t n){auto *a=(uint8_t*)d;auto *b=(const uint8_t*)s;for(size_t i=0;i<n;++i)a[i]=b[i];return d;}
extern "C" void *memset(void *d,int c,size_t n){auto *a=(uint8_t*)d;for(size_t i=0;i<n;++i)a[i]=(uint8_t)c;return d;}
static size_t length(const char *s){size_t n=0;while(s&&s[n])++n;return n;}
static bool equal(const char *a,const char *b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static bool prefix(const char *a,const char *b){while(*b)if(*a++!=*b++)return false;return true;}
static void print(const char *s){syscall6(64,1,(long)s,(long)length(s));syscall6(64,1,(long)"\n",1);}
static void number(uint64_t value){char s[32];unsigned n=0;do{s[n++]=(char)('0'+value%10);value/=10;}while(value);for(unsigned i=0;i<n/2;++i){char c=s[i];s[i]=s[n-i-1];s[n-i-1]=c;}s[n]=0;print(s);}
[[noreturn]]static void die(const char *s){print(s);syscall6(93,1);__builtin_unreachable();}
static void *map(size_t bytes,int prot=3){long p=syscall6(222,0,(long)bytes,prot,0x22,-1,0);if(p<0&&p> -4096)die("runner: mmap failed");return(void*)p;}
static int alloc(uint32_t n,void **out){uint64_t *p=(uint64_t*)map((size_t)n+16);p[0]=n+16;*out=p+2;return 0;}
static int release(void *out){if(out){auto *p=(uint64_t*)out-2;syscall6(215,(long)p,(long)p[0]);}return 0;}
static uint8_t *read_all(const char *path,uint32_t *bytes){long fd=syscall6(56,-100,(long)path,0);if(fd<0)die(path);long n=syscall6(62,fd,0,2);if(n<0||n>32*1024*1024)die("runner: invalid input size");syscall6(62,fd,0,0);auto *p=(uint8_t*)map((size_t)n+1);long at=0;while(at<n){long got=syscall6(63,fd,(long)(p+at),n-at);if(got<=0)die("runner: input read failed");at+=got;}p[n]=0;syscall6(57,fd);*bytes=(uint32_t)n;return p;}
static sacx_api api;
template<class T>struct dummy;
template<class R,class...A>struct dummy<R(*)(A...)>{static R call(A...){return R();}};
template<class...A>struct dummy<void(*)(A...)>{static void call(A...){}};
#define STUB(field) api.field=&dummy<decltype(api.field)>::call
struct obj{int32_t x,y;uint32_t w,h;};static obj objects[2048];static uint32_t next_obj=2;
struct image{uint32_t *pixels,w,h;};static image images[64];static uint32_t next_image=1,last_image;
static uint64_t tick;static unsigned frame;static bool exited,images_enabled,click_navigation;static sacx_update_fn update;
static uint8_t *body;static uint32_t body_bytes;
struct request{bool used;uint32_t code,size;const uint8_t *data;};static request requests[256];static uint32_t next_request;
static uint64_t real_ms(){struct{long seconds,nanos;}t;syscall6(113,1,(long)&t);return(uint64_t)t.seconds*1000+t.nanos/1000000;}
static uint64_t deadline;
static int checkpoint(){if(real_ms()>deadline)die("runner: callback exceeded wall deadline");return 0;}
static int set_update(sacx_update_fn fn){update=fn;return 0;}
static int app_exit(int,const char *text){print(text);exited=true;return 0;}
static uint64_t clock_ms(){return tick;}
static int yes(uint32_t){return 1;}
static int key(uint8_t k){return(frame==1&&k==0x28)||(frame==250&&k==0x45)||(!images_enabled&&frame==0&&k==0x42);}
static int focused(uint32_t){return frame==1;}
static int mouse(sacx_mouse_state *out){memset(out,0,sizeof *out);if(click_navigation){out->x=16;out->y=132;out->buttons=frame==20?1:0;}return 0;}
static int point(uint32_t,int32_t,int32_t){return click_navigation?1:0;}
static int utc(uint64_t *out){*out=1793138400;return 0;}
static int create_window(int32_t,int32_t,uint32_t w,uint32_t h,int32_t,const char*,const sacx_window_style*,uint32_t *out){objects[1]={0,0,w,h};*out=1;return 0;}
static int window_root(uint32_t,uint32_t *out){*out=1;return 0;}
static int title(uint32_t,const char *s){print(s);return 0;}
static int rect_add(int32_t x,int32_t y,uint32_t w,uint32_t h,int32_t,sacx_color,uint32_t,uint32_t *out){if(next_obj>=2048)die("runner: objects exhausted");*out=next_obj++;objects[*out]={x,y,w,h};return 0;}
static int text_add(const char*,int32_t,int32_t,int32_t,sacx_color,uint8_t,uint32_t,int32_t,int32_t,uint32_t,uint32_t,uint32_t *out){*out=next_obj++;return 0;}
static int set_rect(uint32_t o,int32_t x,int32_t y,uint32_t w,uint32_t h){objects[o]={x,y,w,h};return 0;}
static int get_rect(uint32_t o,int32_t *x,int32_t *y,uint32_t *w,uint32_t *h){*x=objects[o].x;*y=objects[o].y;*w=objects[o].w;*h=objects[o].h;return 0;}
static int button(int32_t,int32_t,uint32_t,uint32_t,int32_t,const sacx_button_style*,sacx_button_on_click_fn,void*,uint32_t *out){*out=next_obj++;return 0;}
static int root(uint32_t handle,uint32_t *out){*out=handle;return 0;}
static int textbox(int32_t,int32_t,uint32_t,uint32_t,int32_t,const sacx_textbox_style*,sacx_textbox_on_submit_fn,void*,uint32_t *out){*out=next_obj++;return 0;}
static int text_copy(uint32_t,char *out,uint32_t cap){const char *url="https://fixture.test/";uint32_t n=0;while(url[n]&&n+1<cap){out[n]=url[n];++n;}out[n]=0;return(int)n;}
static int file_open(const char *path,uint32_t flags,uint32_t *out){if(flags!=SACX_FILE_READ||!prefix(path,"0:/OS/System/Fonts/Web/"))return -1;long fd=syscall6(56,-100,(long)(path+3),0);if(fd<0)return -1;*out=(uint32_t)fd;return 0;}
static int file_read(uint32_t fd,void *p,uint32_t n,uint32_t *got){long rc=syscall6(63,fd,(long)p,n);*got=rc<0?0:(uint32_t)rc;return rc<0?-1:0;}
static uint64_t file_size(uint32_t fd){long pos=syscall6(62,fd,0,1),n=syscall6(62,fd,0,2);syscall6(62,fd,pos,0);return(uint64_t)n;}
static int file_close(uint32_t fd){return(int)syscall6(57,fd);}
static int image_create(uint32_t w,uint32_t h,uint32_t color,uint32_t *out){if(next_image>=64||!w||!h||(uint64_t)w*h>16777216)return -1;uint32_t id=next_image++;images[id]={nullptr,w,h};alloc(w*h*4,(void**)&images[id].pixels);for(uint64_t i=0;i<(uint64_t)w*h;++i)images[id].pixels[i]=color;*out=id;last_image=id;return 0;}
static int image_destroy(uint32_t id){release(images[id].pixels);images[id].pixels=nullptr;return 0;}
static int image_pixels(uint32_t id,uint32_t **pixels,uint32_t *stride){*pixels=images[id].pixels;*stride=images[id].w;return 0;}
static int image_publish(uint32_t id,const uint32_t *src,uint32_t stride,uint32_t x,uint32_t y,uint32_t w,uint32_t h){auto &im=images[id];if(!im.pixels||x+w>im.w||y+h>im.h)return -1;for(uint32_t row=0;row<h;++row)memcpy(im.pixels+(uint64_t)(y+row)*im.w+x,src+(uint64_t)(y+row)*stride+x,w*4);return 0;}
static int image_object(uint32_t,int32_t,int32_t,uint32_t *out){*out=next_obj++;return 0;}
static int start_request(const sacx_net_request_desc_ex *desc,uint32_t *out){if(next_request>=255)return -3;uint32_t id=++next_request;bool main=equal(desc->url,"https://fixture.test/"),target=equal(desc->url,"https://target.test/");
static const char landing[]="<!doctype html><title>Link fixture</title><a href='https://target.test/'>Open target</a>";
bool ok=main||(click_navigation&&target);const uint8_t *data=click_navigation&&main?(const uint8_t*)landing:body;
uint32_t n=click_navigation&&main?sizeof(landing)-1:body_bytes;
requests[id]={true,ok?200u:404u,ok?n:0u,ok?data:nullptr};*out=id;print(desc->url);return 0;}
static uint32_t request_status(uint32_t){return SACX_NET_STATUS_DONE;}
static int request_info(uint32_t id,sacx_net_response_info *out){memset(out,0,sizeof *out);out->http_status=requests[id].code;out->body_size=requests[id].size;out->status=SACX_NET_STATUS_DONE;memcpy(out->content_type,"text/html; charset=utf-8",25);return 0;}
static int request_progress(uint32_t id,sacx_net_progress *out){*out={SACX_NET_STATUS_DONE,1,requests[id].size,requests[id].size,1,0};return 0;}
static int request_headers(uint32_t id,uint32_t offset,void *out,uint32_t capacity,uint32_t *got){const char *h=requests[id].code==200?"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n":"HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\n";uint32_t n=(uint32_t)length(h);*got=offset>=n?0:n-offset;if(*got>capacity)*got=capacity;memcpy(out,h+offset,*got);return 0;}
static int request_read(uint32_t id,uint32_t offset,void *out,uint32_t capacity,uint32_t *got){auto &r=requests[id];*got=offset>=r.size?0:r.size-offset;if(*got>capacity)*got=capacity;if(*got)memcpy(out,r.data+offset,*got);return 0;}
static void setup_api(){
    api.abi_version=SACX_API_ABI_VERSION;api.struct_size=sizeof api;
#include "browser_runner_api.inc"
    api.mem_alloc=alloc;api.mem_free=release;api.log=print;api.app_set_update=set_update;api.app_exit=app_exit;
    api.time_monotonic_ms=clock_ms;api.app_checkpoint=checkpoint;
    api.time_utc=utc;api.mouse_get_state=mouse;api.window_point_can_receive_input=point;
    api.window_create_ex=create_window;api.window_root=window_root;api.window_visible=yes;api.window_focused=yes;api.window_set_title=title;
    api.gfx_obj_add_rect=rect_add;api.gfx_obj_add_text=text_add;api.gfx_obj_get_rect=get_rect;api.gfx_obj_set_rect=set_rect;
    api.button_add_rect=button;api.button_root=root;api.textbox_add_rect=textbox;api.textbox_root=root;api.textbox_text_copy=text_copy;api.textbox_focused=focused;api.input_key_pressed=key;
    api.file_open=file_open;api.file_read=file_read;api.file_size=file_size;api.file_close=file_close;
    api.img_create=image_create;api.img_destroy=image_destroy;api.img_pixels=image_pixels;api.img_publish_region=image_publish;api.gfx_obj_add_image_from_img=image_object;
    api.net_request_start_stream=start_request;api.net_request_status=request_status;api.net_response_info=request_info;api.net_response_progress=request_progress;api.net_response_headers_read=request_headers;api.net_response_read_available=request_read;
}
static void *load_app(const char *path){uint32_t size;uint8_t *data=read_all(path,&size);if(size<sizeof(sacx_header))die("runner: short SACX");
    auto *h=(sacx_header*)data;
    if(h->version==SACX_FAT_VERSION){auto *fat=(sacx_fat_header*)data;if(fat->slice_count>8||fat->slice_offset+(uint64_t)fat->slice_count*sizeof(sacx_slice)>size)die("runner: invalid slices");auto *s=(sacx_slice*)(data+fat->slice_offset);bool found=false;for(unsigned i=0;i<fat->slice_count;++i)if(s[i].arch==SACX_ARCH_AA64){if((uint64_t)s[i].file_offset+s[i].file_size>size)die("runner: slice overflow");h=(sacx_header*)(data+s[i].file_offset);size=s[i].file_size;data=(uint8_t*)h;found=true;break;}if(!found)die("runner: no ARM64 slice");}
    if(h->magic!=SACX_MAGIC||h->version!=1||!h->image_size||h->image_size>128u*1024u*1024u||h->entry_rva>=h->image_size||h->segment_count>64||h->reloc_count>8192||h->segment_offset+(uint64_t)h->segment_count*sizeof(sacx_segment)>size||h->reloc_offset+(uint64_t)h->reloc_count*sizeof(sacx_reloc)>size)die("runner: invalid payload");
    auto *image=(uint8_t*)map(h->image_size);
    auto *segments=(sacx_segment*)(data+h->segment_offset);for(unsigned i=0;i<h->segment_count;++i){auto &s=segments[i];if((uint64_t)s.rva+s.mem_size>h->image_size||(uint64_t)h->image_offset+s.file_offset+s.file_size>size||s.file_size>s.mem_size)die("runner: segment overflow");memcpy(image+s.rva,data+h->image_offset+s.file_offset,s.file_size);}
    auto *relocs=(sacx_reloc*)(data+h->reloc_offset);for(unsigned i=0;i<h->reloc_count;++i){auto &r=relocs[i];if(r.type!=1||(uint64_t)r.target_rva+8>h->image_size||r.addend>=h->image_size)die("runner: relocation overflow");*(uint64_t*)(image+r.target_rva)=(uint64_t)(image+r.addend);}
    if(syscall6(226,(long)image,h->image_size,7))die("runner: executable protection failed");
    return image+h->entry_rva;
}
static void dump_image(const char *path){auto &im=images[last_image];if(!im.pixels)die("runner: no viewport");long fd=syscall6(56,-100,(long)path,577,0644);if(fd<0)die("runner: output open failed");
    char header[64];unsigned n=0;auto text=[&](const char *s){while(*s)header[n++]=*s++;};auto integer=[&](uint32_t v){char b[16];unsigned k=0;do{b[k++]=(char)('0'+v%10);v/=10;}while(v);while(k)header[n++]=b[--k];};text("P6\n");integer(im.w);text(" ");integer(im.h);text("\n255\n");syscall6(64,fd,(long)header,n);
    auto *row=(uint8_t*)map(im.w*3);uint64_t nonwhite=0;for(unsigned y=0;y<im.h;++y){for(unsigned x=0;x<im.w;++x){uint32_t p=im.pixels[(uint64_t)y*im.w+x];row[x*3]=(uint8_t)(p>>16);row[x*3+1]=(uint8_t)(p>>8);row[x*3+2]=(uint8_t)p;if((p&0xffffff)!=0xffffff)++nonwhite;}syscall6(64,fd,(long)row,im.w*3);}syscall6(57,fd);print("runner: nonwhite pixels");number(nonwhite);
}
static int (*app_entry)(const sacx_api*);
static void run_app(void*){
 if(app_entry(&api)||!update||exited)die("runner: startup failed");
 kfiber_yield();
 for(;;){update(&api);kfiber_yield();}
}
extern "C" int host_main(long argc,char **argv){if(argc<4)die("usage: runner app.sacx fixture.html output.ppm [images]");body=read_all(argv[2],&body_bytes);for(int i=4;i<argc;++i){if(equal(argv[i],"images"))images_enabled=true;if(equal(argv[i],"click"))click_navigation=true;}setup_api();deadline=real_ms()+15000;app_entry=(int(*)(const sacx_api*))load_app(argv[1]);print("runner: starting production ARM64 binary on a guarded 256 KiB kernel fiber stack");
 auto *stack=(uint8_t*)map(256*1024+8192,0);if(syscall6(226,(long)(stack+4096),256*1024,3))die("runner: stack protection failed");
 memset(stack+4096,0xa5,256*1024);kfiber fiber;
 if(kfiber_init(&fiber,stack+4096,256*1024,run_app,nullptr)||kfiber_resume(&fiber))die("runner: fiber startup failed");
 for(frame=0;frame<300&&!exited;++frame){tick+=16;checkpoint();if(kfiber_resume(&fiber))die("runner: callback continuation failed");}
 uint32_t untouched=0;while(untouched<256*1024&&stack[4096+untouched]==0xa5)++untouched;print("runner: stack high-water bytes");number(256*1024-untouched);dump_image(argv[3]);return 0;}
extern "C" __attribute__((naked)) void _start(){asm volatile("mov x0,sp\n ldr x0,[x0]\n add x1,sp,#8\n bl host_main\n mov x8,#93\n svc #0\n");}
