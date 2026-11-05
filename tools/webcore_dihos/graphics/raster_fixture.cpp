#include "sacx_user_api.h"
#include "cairo_mutex_dihos.h"
#include <cairo.h>
#include <setjmp.h>
#include <atomic>
#include <cstdlib>
#include <thread>
#include <vector>

static std::atomic<int> fail_after{-1};
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t,size_t);
extern "C" void* __real_realloc(void*,size_t);
static bool failAllocation(){
    int count=fail_after.load();
    while(count>=0){if(fail_after.compare_exchange_weak(count,count?count-1:-1))return !count;}
    return false;
}
extern "C" void* __wrap_malloc(size_t size){return failAllocation()?nullptr:__real_malloc(size);}
extern "C" void* __wrap_calloc(size_t n,size_t size){return failAllocation()?nullptr:__real_calloc(n,size);}
extern "C" void* __wrap_realloc(void* p,size_t size){return failAllocation()?nullptr:__real_realloc(p,size);}
static void check(bool value,const char* why){if(!value)sacx_user_exit(177,why);}
static void jumpTests(){
    struct {uint64_t before; jmp_buf state; uint64_t after;} frame;
    frame.before=0x123456789abcdef0ull;frame.after=~frame.before;
    for(int expected:{0,1,7}){
        volatile int marker=0;
        int result=setjmp(frame.state);
        if(!result){marker=42;longjmp(frame.state,expected);}
        check(result==(expected?expected:1) && marker==42,"Cairo: native jump result");
        check(frame.before==0x123456789abcdef0ull && frame.after==~frame.before,"Cairo: jump-buffer bounds");
    }
}
static void mutexTests(){
    dihos_cairo_mutex mutex{};dihos_cairo_mutex_init(&mutex);
    dihos_cairo_mutex_lock(&mutex);check(dihos_cairo_mutex_trylock(&mutex),"Cairo: recursive try-lock");
    std::atomic<bool> checked{false};
    std::thread contender([&]{check(!dihos_cairo_mutex_trylock(&mutex),"Cairo: foreign try-lock succeeded");checked=true;});
    contender.join();check(checked,"Cairo: contender not scheduled");
    dihos_cairo_mutex_unlock(&mutex);dihos_cairo_mutex_unlock(&mutex);
    unsigned count=0;
    auto worker=[&]{for(unsigned i=0;i<64;++i){dihos_cairo_mutex_lock(&mutex);++count;dihos_cairo_mutex_unlock(&mutex);}};
    std::thread first(worker),second(worker),third(worker);first.join();second.join();third.join();
    check(count==192,"Cairo: contended mutex lost writes");dihos_cairo_mutex_fini(&mutex);
}
static void drawTests(){
    constexpr unsigned width=128,height=96,guard=8;
    std::vector<uint32_t> memory(width*height+2*guard,0xdeadbeef);
    auto* pixels=memory.data()+guard;
    auto* surface=cairo_image_surface_create_for_data(reinterpret_cast<unsigned char*>(pixels),CAIRO_FORMAT_ARGB32,width,height,width*4);
    check(cairo_surface_status(surface)==CAIRO_STATUS_SUCCESS,"Cairo: image surface");
    auto* cr=cairo_create(surface);cairo_set_source_rgb(cr,0,0,0);cairo_paint(cr);
    cairo_set_source_rgb(cr,1,0,0);cairo_rectangle(cr,10,10,20,15);cairo_fill(cr);
    cairo_save(cr);cairo_rectangle(cr,0,0,5,5);cairo_clip(cr);cairo_set_source_rgb(cr,0,1,0);cairo_paint(cr);cairo_restore(cr);
    cairo_surface_flush(surface);
    check(pixels[0]==0xff00ff00 && pixels[15*width+15]==0xffff0000 && pixels[30*width+30]==0xff000000,"Cairo: fill or clip pixels");
    cairo_set_source_rgba(cr,0,0,1,0.5);cairo_rectangle(cr,10,10,20,15);cairo_fill(cr);cairo_surface_flush(surface);
    auto blended=pixels[15*width+15];
    check((blended>>24)==255 && ((blended>>16)&255)>=127 && ((blended>>16)&255)<=128 && (blended&255)>=127 && (blended&255)<=128,"Cairo: premultiplied alpha composition");
    auto* gradient=cairo_pattern_create_linear(0,0,width,0);
    cairo_pattern_add_color_stop_rgb(gradient,0,0,0,0);cairo_pattern_add_color_stop_rgb(gradient,1,1,1,1);
    cairo_set_source(cr,gradient);cairo_paint(cr);cairo_surface_flush(surface);
    check((pixels[width/2]&255)>110 && (pixels[width/2]&255)<145 && (pixels[1]&255)<20 && (pixels[width-2]&255)>230,"Cairo: gradient rasterization");
    cairo_pattern_destroy(gradient);
    cairo_set_source_rgb(cr,0,1,0);cairo_translate(cr,64,48);cairo_rotate(cr,0.7853981633974483);
    cairo_rectangle(cr,-10,-10,20,20);cairo_fill(cr);cairo_surface_flush(surface);
    check(pixels[48*width+64]==0xff00ff00,"Cairo: transformed path");
    check(cairo_status(cr)==CAIRO_STATUS_SUCCESS,"Cairo: drawing status");
    cairo_destroy(cr);cairo_surface_destroy(surface);
    for(unsigned i=0;i<guard;++i)check(memory[i]==0xdeadbeef && memory[memory.size()-1-i]==0xdeadbeef,"Cairo: write outside image");
    auto* invalid=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,-1,10);
    check(cairo_surface_status(invalid)==CAIRO_STATUS_INVALID_SIZE,"Cairo: invalid dimensions");cairo_surface_destroy(invalid);
}
static void parallelDrawTests(){
    auto worker=[]{for(unsigned i=0;i<8;++i){
        auto* surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,32,32);auto* cr=cairo_create(surface);
        cairo_set_source_rgb(cr,0.25,0.5,0.75);cairo_paint(cr);cairo_surface_flush(surface);
        check(cairo_status(cr)==CAIRO_STATUS_SUCCESS,"Cairo: concurrent surface");
        auto* bytes=cairo_image_surface_get_data(surface);check(bytes && bytes[3]==255,"Cairo: concurrent output");
        cairo_destroy(cr);cairo_surface_destroy(surface);
    }};
    std::thread first(worker),second(worker),third(worker);first.join();second.join();third.join();
}
static void scaledImageTests(){
    auto* input=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,8,8);
    auto* output=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,64,64);
    check(cairo_surface_status(input)==CAIRO_STATUS_SUCCESS && cairo_surface_status(output)==CAIRO_STATUS_SUCCESS,"Cairo: scaled image allocation");
    auto* pixels=reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(input));
    const unsigned stride=cairo_image_surface_get_stride(input)/4;
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)pixels[y*stride+x]=x<4?0xffff0000:0xff0000ff;
    cairo_surface_mark_dirty(input);
    auto* cr=cairo_create(output);cairo_scale(cr,8,8);cairo_set_source_surface(cr,input,0,0);
    cairo_pattern_set_filter(cairo_get_source(cr),CAIRO_FILTER_BILINEAR);
    cairo_pattern_set_extend(cairo_get_source(cr),CAIRO_EXTEND_PAD);cairo_paint(cr);cairo_surface_flush(output);
    pixels=reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(output));
    const unsigned out_stride=cairo_image_surface_get_stride(output)/4;
    auto middle=pixels[32*out_stride+31];
    check(pixels[32*out_stride+16]==0xffff0000 && pixels[32*out_stride+48]==0xff0000ff &&
          ((middle>>16)&255)>90 && ((middle>>16)&255)<165 && (middle&255)>90 && (middle&255)<165,
          "Cairo: bilinear scaling pixels");
    check(cairo_status(cr)==CAIRO_STATUS_SUCCESS,"Cairo: bilinear scaling status");
    cairo_destroy(cr);cairo_surface_destroy(input);cairo_surface_destroy(output);
}
static void allocationTests(){
#ifdef DIHOS_CAIRO_CRITICAL_OOM
    const int first=0,last=12;
#else
    const int first=0,last=4;
#endif
    for(int point=first;point<last;++point){
        static const char* stages[]={"Cairo OOM: 0","Cairo OOM: 1","Cairo OOM: 2","Cairo OOM: 3","Cairo OOM: 4","Cairo OOM: 5","Cairo OOM: 6","Cairo OOM: 7","Cairo OOM: 8","Cairo OOM: 9","Cairo OOM: 10","Cairo OOM: 11"};
        sacx_user_log(stages[point]);
        cairo_debug_reset_static_data();fail_after=point;
        auto* surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,64,64);auto* cr=cairo_create(surface);
        for(unsigned i=0;i<32;++i){cairo_move_to(cr,i,i);cairo_line_to(cr,63-i,i);cairo_line_to(cr,i,63-i);cairo_close_path(cr);}
        cairo_fill(cr);fail_after=-1;
        auto status=cairo_status(cr);check(status==CAIRO_STATUS_SUCCESS || status==CAIRO_STATUS_NO_MEMORY,"Cairo: allocation failure status");
        cairo_destroy(cr);cairo_surface_destroy(surface);
    }
    drawTests();cairo_debug_reset_static_data();
}
extern "C" int dihos_user_main(const sacx_api* api){
#ifdef DIHOS_CAIRO_CRITICAL_OOM
    allocationTests();sacx_user_exit(177,"Cairo: expected critical OOM exit missing");
#endif
    jumpTests();mutexTests();api->log("Cairo native: jumps, recursive mutex and contention OK");
    drawTests();scaledImageTests();parallelDrawTests();api->log("Cairo native: pixels, clips, alpha, gradients, transforms, bilinear scaling and concurrent surfaces OK");
    allocationTests();api->log("SACX Cairo: native rasterization, threading and allocation recovery OK");return 113;
}
