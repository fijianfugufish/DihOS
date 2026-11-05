#include "process/dihos_user_window.h"
#include "memory/pmem.h"
#define WINDOW_SLOTS 4u
typedef struct window_slot {
 uint64_t owner,generation,pages;uint32_t *front,*back,width,height,written,head,count;
 sacx_user_window_event events[SACX_USER_WINDOW_EVENTS];
 sacx_user_window_region region;uint32_t region_active;
} window_slot;
static window_slot windows[WINDOW_SLOTS];
static dihos_user_window_backend backend;
static uint64_t token(unsigned i){return (windows[i].generation<<8)|(i+1u);}
static window_slot *lookup(uint64_t id){unsigned i=(unsigned)(id&255);if(!i || i>WINDOW_SLOTS)return 0;window_slot *s=&windows[i-1];return s->owner && s->generation==(id>>8)?s:0;}
uint64_t dihos_user_window_owner(uint64_t id){window_slot *s=lookup(id);return s?s->owner:0;}
int dihos_user_window_set_backend(const dihos_user_window_backend *b){
 for(unsigned i=0;i<WINDOW_SLOTS;++i)if(windows[i].owner)return -16;
 if(!b || !b->create || !b->present || !b->destroy)return -22;
 backend=*b;return 0;
}
static void dispose(unsigned i){window_slot *s=&windows[i];backend.destroy(token(i));pmem_free_pages(s->front,s->pages);pmem_free_pages(s->back,s->pages);uint64_t generation=s->generation;*s=(window_slot){0};s->generation=generation;}
void dihos_user_window_cleanup(uint64_t owner){if(owner)for(unsigned i=0;i<WINDOW_SLOTS;++i)if(windows[i].owner==owner)dispose(i);}
int dihos_user_window_push(uint64_t id,const sacx_user_window_event *event){
 window_slot *s=lookup(id);if(!s || !event || event->type<SACX_WINDOW_MOVE || event->type>SACX_WINDOW_KEY_UP || event->reserved)return -22;
 if(event->type==SACX_WINDOW_KEY_DOWN || event->type==SACX_WINDOW_KEY_UP){
  if(event->buttons || event->x<4 || event->x>255 || event->y<0 || event->y>0x10ffff ||
     (event->y>=0xd800 && event->y<=0xdfff) || event->delta<0 || (event->delta&~31))return -22;
 }
 if(event->type==SACX_WINDOW_MOVE && s->count){unsigned tail=(s->head+s->count-1)%SACX_USER_WINDOW_EVENTS;if(s->events[tail].type==SACX_WINDOW_MOVE){s->events[tail]=*event;return 0;}}
 if(s->count==SACX_USER_WINDOW_EVENTS){s->head=0;s->count=1;s->events[0]=(sacx_user_window_event){.type=SACX_WINDOW_RESET,.time_ms=event->time_ms};}
 s->events[(s->head+s->count)%SACX_USER_WINDOW_EVENTS]=*event;++s->count;return 0;
}
int64_t dihos_user_window_call(const aarch64_user_vm *vm,uint64_t owner,uint64_t op,const uint64_t a[3]){
 if(!owner)return -1;
 if(op==SACX_USER_WINDOW_CREATE){
  if(a[1]!=sizeof(sacx_user_window_desc) || a[2])return -22;
  sacx_user_window_desc d;int rc=aarch64_user_vm_copy_in(vm,&d,a[0],sizeof(d));if(rc)return rc;
  if(!d.width || !d.height || d.width>SACX_USER_WINDOW_MAX_WIDTH || d.height>SACX_USER_WINDOW_MAX_HEIGHT)return -22;
  unsigned end=0;while(end<sizeof(d.title) && d.title[end]){if((unsigned char)d.title[end]<32 || (unsigned char)d.title[end]>126)return -22;++end;}
  if(end==sizeof(d.title))return -22;
  if(!backend.create)return -38;
  unsigned index=WINDOW_SLOTS;
  for(unsigned i=0;i<WINDOW_SLOTS;++i){if(windows[i].owner==owner)return -24;if(index==WINDOW_SLOTS && !windows[i].owner && windows[i].generation<0x7fffffffffffffull)index=i;}
  if(index==WINDOW_SLOTS)return -24;
  uint64_t pages=((uint64_t)d.width*d.height*4+4095)/4096;
  uint32_t *front=pmem_alloc_pages(pages);if(!front)return -12;
  uint32_t *back=pmem_alloc_pages(pages);if(!back){pmem_free_pages(front,pages);return -12;}
  for(uint64_t i=0;i<pages*1024;++i)front[i]=back[i]=0xffffffffu;
  window_slot *s=&windows[index];++s->generation;
  s->owner=owner;
  rc=backend.create(token(index),&d,front);
  if(rc){s->owner=0;pmem_free_pages(front,pages);pmem_free_pages(back,pages);return rc;}
  s->owner=owner;s->front=front;s->back=back;s->pages=pages;s->width=d.width;s->height=d.height;
  return (int64_t)token(index);
 }
 window_slot *s=lookup(a[0]);if(!s || s->owner!=owner)return -9;
 if(op==SACX_USER_WINDOW_CLOSE){if(a[1] || a[2])return -22;dispose((unsigned)(a[0]&255)-1);return 0;}
 if(op==SACX_USER_WINDOW_REGION){
  if(a[2]!=sizeof(sacx_user_window_region))return -22;
  if(s->written || s->region_active)return -16;
  sacx_user_window_region r;int rc=aarch64_user_vm_copy_in(vm,&r,a[1],sizeof(r));if(rc)return rc;
  if(!r.width || !r.height || r.x>=s->width || r.y>=s->height ||
     r.width>s->width-r.x || r.height>s->height-r.y ||
     (uint64_t)r.width*r.height>SACX_USER_WINDOW_WRITE_MAX)return -22;
  s->region=r;s->region_active=1;return 0;
 }
 if(op==SACX_USER_WINDOW_WRITE){
  uint32_t offset=(uint32_t)(a[2]>>32),n=(uint32_t)a[2],total=s->width*s->height;
  if(s->region_active)total=s->region.width*s->region.height;
  if(!n || n>SACX_USER_WINDOW_WRITE_MAX || offset!=s->written || offset>total || n>total-offset)return -22;
  uint64_t bytes=(uint64_t)n*4;
  if(a[1]>UINT64_MAX-bytes)return -14;
  // Keep VM checks/copies bounded at 4 KiB while amortising syscall entry and
  // desktop scheduling across up to 64 KiB. Validate every chunk before any
  // mutation; a bad later page cannot advance or partially admit a frame.
  for(uint64_t at=0;at<bytes;at+=4096){uint64_t count=bytes-at;if(count>4096)count=4096;
   int rc=aarch64_user_vm_check_buffer(vm,a[1]+at,count,0);if(rc)return rc;}
  if(s->region_active){
   for(uint32_t at=0;at<n;){uint32_t logical=offset+at,row=logical/s->region.width,col=logical%s->region.width;
    uint32_t count=s->region.width-col;if(count>n-at)count=n-at;if(count>1024)count=1024;
    int rc=aarch64_user_vm_copy_in(vm,s->back+(s->region.y+row)*s->width+s->region.x+col,a[1]+(uint64_t)at*4,(uint64_t)count*4);if(rc)return rc;at+=count;}
  }else for(uint64_t at=0;at<bytes;at+=4096){uint64_t count=bytes-at;if(count>4096)count=4096;
   int rc=aarch64_user_vm_copy_in(vm,(uint8_t*)(s->back+offset)+at,a[1]+at,count);if(rc)return rc;}
  s->written+=n;return 0;
 }
 if(op==SACX_USER_WINDOW_PRESENT){
  if(a[1] || a[2])return -22;
  if(s->region_active){
   if(s->written!=s->region.width*s->region.height)return -11;
   for(uint32_t y=0;y<s->region.height;++y){uint32_t at=(s->region.y+y)*s->width+s->region.x;
    for(uint32_t x=0;x<s->region.width;++x)s->front[at+x]=s->back[at+x];}
   backend.present(a[0],s->front);s->region_active=0;s->written=0;return 0;
  }
  if(s->written!=s->width*s->height)return -11;
  backend.present(a[0],s->back);uint32_t *old=s->front;s->front=s->back;s->back=old;s->written=0;return 0;
 }
 if(op==SACX_USER_WINDOW_EVENT){
  if(a[2]!=sizeof(sacx_user_window_event))return -22;
  int rc=aarch64_user_vm_check_buffer(vm,a[1],a[2],1);if(rc)return rc;
  if(!s->count)return -11;
  rc=aarch64_user_vm_copy_out(vm,a[1],&s->events[s->head],a[2]);if(rc)return rc;
  s->head=(s->head+1)%SACX_USER_WINDOW_EVENTS;--s->count;return 0;
 }
 return -38;
}
