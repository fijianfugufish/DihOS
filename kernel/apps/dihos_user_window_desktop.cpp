#include "process/dihos_user_window.h"
#include "kwrappers/kwindow.h"
#include "kwrappers/kmouse.h"
#include "kwrappers/kinput.h"
#include "../../sdk/sacx/include/sacx_window_keys.h"
#include "system/dihos_time.h"
extern "C" int sacx_browser_surface_create(uint64_t,const sacx_user_window_desc*,const uint32_t*);
extern "C" int sacx_browser_surface_present(uint64_t,const uint32_t*);
extern "C" int sacx_browser_surface_destroy(uint64_t);
#pragma weak sacx_browser_surface_create
#pragma weak sacx_browser_surface_present
#pragma weak sacx_browser_surface_destroy

// Desktop adapter only; the broker owns frame storage and checks EL0 buffers.
static const kfont *window_font;
static struct desktop_window {uint64_t id;kwindow_handle window;kgfx_obj_handle image;uint32_t buttons;int32_t x,y;uint8_t close_sent,inside,focused;uint8_t keys[256];uint64_t repeat_at[256];} slots[4];
static int create_window(uint64_t id,const sacx_user_window_desc *d,const uint32_t *pixels){
 if(sacx_browser_surface_create){int rc=sacx_browser_surface_create(id,d,pixels);if(rc!=-38)return rc;}
 unsigned i=(unsigned)(id&255)-1;if(i>=4 || !window_font)return -38;
 kwindow_style style=kwindow_style_default();style.body_fill=white;
 kwindow_handle w=kwindow_create(100,80,d->width+4,d->height+46,30,window_font,d->title,&style);
 if(w.idx<0)return -12;
 kgfx_obj_handle image=kgfx_obj_add_image(pixels,d->width,d->height,2,44,d->width);
 if(image.idx<0){kwindow_destroy(w);return -12;}
 kgfx_obj_set_parent(image,kwindow_root(w));kgfx_obj_set_clip_to_parent(image,1);
 slots[i]={};slots[i].id=id;slots[i].window=w;slots[i].image=image;
 kwindow_set_close_deferred(w,1);kwindow_raise(w);return 0;
}
static void present_window(uint64_t id,const uint32_t *pixels){
 if(sacx_browser_surface_present && sacx_browser_surface_present(id,pixels))return;
 unsigned i=(unsigned)(id&255)-1;if(i>=4 || slots[i].id!=id)return;
 kgfx_obj *image=kgfx_obj_ref(slots[i].image);if(image){image->u.image.argb=pixels;kgfx_image_touch(slots[i].image);}
}
static void destroy_window(uint64_t id){
 if(sacx_browser_surface_destroy && sacx_browser_surface_destroy(id))return;
 unsigned i=(unsigned)(id&255)-1;if(i>=4 || slots[i].id!=id)return;
 kgfx_obj_destroy(slots[i].image);kwindow_destroy(slots[i].window);slots[i]={};
}
extern "C" void dihos_user_window_desktop_init(const kfont *font){
 window_font=font;static const dihos_user_window_backend backend={create_window,present_window,destroy_window};
 (void)dihos_user_window_set_backend(&backend);
}
extern "C" void dihos_user_window_desktop_poll(void){
 static uint32_t previous_buttons;uint32_t buttons=kmouse_buttons()&1u;int32_t mx=kmouse_x(),my=kmouse_y();uint64_t now=dihos_time_monotonic_ms();
 for(unsigned i=0;i<4;++i){auto& s=slots[i];if(!s.id)continue;
  sacx_user_window_event e={};e.time_ms=now;
  if(!s.close_sent && kwindow_close_requested(s.window)){s.close_sent=1;e.type=SACX_WINDOW_CLOSE;dihos_user_window_push(s.id,&e);}
  if(s.close_sent)continue;
  bool focused=kwindow_focused(s.window);
  if(s.focused && !focused){
   e.type=SACX_WINDOW_RESET;dihos_user_window_push(s.id,&e);s.buttons=0;
   for(unsigned key=0;key<256;++key){s.keys[key]=0;s.repeat_at[key]=0;}
  }
  unsigned modifiers=(kinput_key_down(KEY_LSHIFT)||kinput_key_down(KEY_RSHIFT)?SACX_WINDOW_SHIFT:0)
   |(kinput_key_down(KEY_LCTRL)||kinput_key_down(KEY_RCTRL)?SACX_WINDOW_CTRL:0)
   |(kinput_key_down(KEY_LALT)||kinput_key_down(KEY_RALT)?SACX_WINDOW_ALT:0)
   |(kinput_key_down(KEY_LGUI)||kinput_key_down(KEY_RGUI)?SACX_WINDOW_META:0);
  if(focused)for(unsigned key=4;key<256;++key){
   bool down=kinput_key_down((uint8_t)key);
   // Do not deliver keys held while another window had focus. A new press is
   // admitted only after focus acquisition (or if it occurred in this frame).
   if(!s.focused){s.keys[key]=(uint8_t)down;s.repeat_at[key]=down?UINT64_MAX:0;}
   bool pressed=kinput_key_pressed((uint8_t)key);
   bool repeat=down && s.keys[key] && now>=s.repeat_at[key] && key<0xe0;
   if((down && !s.keys[key]) || pressed || repeat || (!down && s.keys[key])){
    e={};e.time_ms=now;e.type=down || pressed?SACX_WINDOW_KEY_DOWN:SACX_WINDOW_KEY_UP;
    e.x=(int32_t)key;e.delta=(int32_t)(modifiers|(repeat?SACX_WINDOW_REPEAT:0));
    e.y=e.type==SACX_WINDOW_KEY_DOWN?(int32_t)sacx_window_key_text(key,modifiers):0;
    dihos_user_window_push(s.id,&e);
    if(pressed && !down){e.type=SACX_WINDOW_KEY_UP;e.y=0;dihos_user_window_push(s.id,&e);}
    s.repeat_at[key]=now+(repeat?40:400);
   }
   s.keys[key]=(uint8_t)down;
  }
  s.focused=(uint8_t)focused;
  e={};e.time_ms=now;
  kgfx_obj *root=kgfx_obj_ref(kwindow_root(s.window));kgfx_obj *image=kgfx_obj_ref(s.image);if(!root || !image)continue;
  e.x=mx-root->u.rect.x-2;e.y=my-root->u.rect.y-44;
  int inside=e.x>=0 && e.y>=0 && (uint32_t)e.x<image->u.image.w && (uint32_t)e.y<image->u.image.h && kwindow_point_can_receive_input(s.window,mx,my);
  if(s.buttons && !kwindow_focused(s.window)){e.type=SACX_WINDOW_RESET;dihos_user_window_push(s.id,&e);s.buttons=0;}
  if(inside || s.buttons){
   if(e.x!=s.x || e.y!=s.y || !s.inside){e.type=SACX_WINDOW_MOVE;e.buttons=s.buttons;dihos_user_window_push(s.id,&e);}
   if(inside && (buttons&~previous_buttons)){s.buttons=1;e.type=SACX_WINDOW_DOWN;e.buttons=1;dihos_user_window_push(s.id,&e);}
   if(s.buttons && !buttons){s.buttons=0;e.type=SACX_WINDOW_UP;e.buttons=0;dihos_user_window_push(s.id,&e);}
   if(inside && kmouse_wheel()){e.type=SACX_WINDOW_WHEEL;e.delta=kmouse_wheel();e.buttons=s.buttons;dihos_user_window_push(s.id,&e);}
  }
  s.x=e.x;s.y=e.y;s.inside=(uint8_t)inside;
 }
 previous_buttons=buttons;
}
