#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/kwrappers/kbutton.c"
static kgfx_obj objects[32];
static uint32_t used_objects;
static kmouse_state pointer;
static kfb framebuffer={.width=1920,.height=1080};
static int allowed=1, clicks;
const kfb *kgfx_info(void) { return &framebuffer; }
kgfx_obj *kgfx_obj_ref(kgfx_obj_handle h)
{ return h.idx>=0 && (uint32_t)h.idx<used_objects && h.generation==1 ? &objects[h.idx] : 0; }
kgfx_obj_handle kgfx_obj_add_rect(int32_t x,int32_t y,uint32_t w,uint32_t h,int32_t z,kcolor fill,uint8_t visible)
{
    assert(used_objects<32);
    kgfx_obj_handle handle={(int)used_objects++,1};
    kgfx_obj *o=kgfx_obj_ref(handle);memset(o,0,sizeof(*o));
    o->kind=KGFX_OBJ_RECT;o->u.rect=(kgfx_rect_data){x,y,w,h};o->z=z;o->fill=fill;o->visible=visible;o->parent_idx=-1;
    return handle;
}
int kgfx_obj_destroy(kgfx_obj_handle h) { kgfx_obj_ref(h)->visible=0; return 0; }
void kmouse_get_state(kmouse_state *s) { *s=pointer; }
int kmouse_set_cursor(kmouse_cursor c) { (void)c;return 0; }
int kwindow_obj_can_receive_input(kgfx_obj_handle h,int32_t x,int32_t y) { (void)h;(void)x;(void)y;return allowed; }
void hyperv_trace(const char *s,int32_t a,int32_t b,int32_t c,int32_t d) { (void)s;(void)a;(void)b;(void)c;(void)d; }
static void clicked(kbutton_handle h,void *ctx) { (void)h;(void)ctx;++clicks; }
static void click_at(int x,int y)
{ pointer.x=x;pointer.y=y;pointer.buttons=1;kbutton_update_all();pointer.buttons=0;kbutton_update_all(); }
int main(void)
{
    kbutton_init();
    kgfx_obj_handle window=kgfx_obj_add_rect(100,80,400,300,100,black,1);
    kgfx_obj_handle viewport=kgfx_obj_add_rect(10,30,200,30,0,black,1);
    kgfx_obj_set_parent(viewport,window);kgfx_obj_set_clip_to_parent(viewport,1);
    kbutton_handle row=kbutton_add_rect(0,20,200,20,2,0,clicked,0);
    kgfx_obj_set_parent(kbutton_root(row),viewport);kgfx_obj_set_clip_to_parent(kbutton_root(row),1);
    click_at(111,131);assert(clicks==1);
    click_at(111,139);assert(clicks==2);
    click_at(111,140);assert(clicks==2); /* clipped bottom */
    click_at(109,131);assert(clicks==2);
    objects[window.idx].u.rect.x+=300;
    click_at(111,131);assert(clicks==2);click_at(411,131);assert(clicks==3);
    allowed=0;click_at(411,131);assert(clicks==3);allowed=1;
    kbutton_set_enabled(row,0);click_at(411,131);assert(clicks==3);kbutton_set_enabled(row,1);
    objects[viewport.idx].visible=0;click_at(411,131);assert(clicks==3);objects[viewport.idx].visible=1;
    pointer.x=411;pointer.y=131;pointer.buttons=1;kbutton_update_all();pointer.x=650;pointer.buttons=0;kbutton_update_all();assert(clicks==3);
    framebuffer.width=640;framebuffer.height=480;click_at(411,131);assert(clicks==4);
    puts("PASS: production button hit testing, nested clipping, moved windows, focus gating, disabled/hidden controls, release outside, 640x480 fallback");
    return 0;
}
