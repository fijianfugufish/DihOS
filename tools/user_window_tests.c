#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "process/dihos_user_window.h"
static _Alignas(16) unsigned char user[131072];static unsigned allocations,alloc_calls,fail_at,created,presented,destroyed,copies;static const uint32_t *visible;static uint64_t protected_page;
void *pmem_alloc_pages(uint64_t n){if(++alloc_calls==fail_at)return 0;void *p=malloc((size_t)n*4096);if(p)++allocations;return p;}
void pmem_free_pages(void *p,uint64_t n){(void)n;assert(p && allocations);--allocations;free(p);}
int aarch64_user_vm_check_buffer(const aarch64_user_vm *v,uint64_t p,uint64_t n,int w){(void)v;(void)w;return p<0x1000 || n>4096 || n>sizeof(user) || p-0x1000>sizeof(user)-n || (protected_page && p<protected_page+4096 && p+n>protected_page)?-14:0;}
int aarch64_user_vm_copy_in(const aarch64_user_vm *v,void *dst,uint64_t p,uint64_t n){if(aarch64_user_vm_check_buffer(v,p,n,0))return -14;++copies;memcpy(dst,user+p-0x1000,(size_t)n);return 0;}
int aarch64_user_vm_copy_out(const aarch64_user_vm *v,uint64_t p,const void *src,uint64_t n){if(aarch64_user_vm_check_buffer(v,p,n,1))return -14;memcpy(user+p-0x1000,src,(size_t)n);return 0;}
static int open_window(uint64_t h,const sacx_user_window_desc *d,const uint32_t *p){assert(h && ((d->width==64 && d->height==32)||(d->width==256 && d->height==128)) && p[0]==0xffffffff);visible=p;++created;return 0;}
static void show(uint64_t h,const uint32_t *p){assert(h && p);visible=p;++presented;}
static void close_window(uint64_t h){assert(h);++destroyed;visible=0;}
static int64_t call(uint64_t owner,uint64_t op,uint64_t a,uint64_t b,uint64_t c){uint64_t args[]={a,b,c};return dihos_user_window_call(0,owner,op,args);}
static int64_t create(uint64_t owner){sacx_user_window_desc d={64,32,"Owned surface"};memcpy(user,&d,sizeof(d));return call(owner,SACX_USER_WINDOW_CREATE,0x1000,sizeof(d),0);}
int main(void){
 assert(create(1)==-38);dihos_user_window_backend backend={open_window,show,close_window};assert(!dihos_user_window_set_backend(&backend));
 fail_at=alloc_calls+2;assert(create(1)==-12 && !allocations && !created);fail_at=0;
 int64_t h=create(1);assert(h>0 && allocations==2 && created==1);assert(create(1)==-24 && allocations==2);
 assert(dihos_user_window_set_backend(&backend)==-16);
 assert(call(2,SACX_USER_WINDOW_PRESENT,h,0,0)==-9);assert(call(1,SACX_USER_WINDOW_PRESENT,h,0,0)==-11);
 for(unsigned i=0;i<2048;++i)((uint32_t*)user)[i]=0xff123456;
 assert(call(1,SACX_USER_WINDOW_WRITE,h,0xffffffffffffffffull,1024)==-14 && visible[0]==0xffffffff);
 assert(call(1,SACX_USER_WINDOW_WRITE,h,0x1000,4097)==-22);
 assert(call(1,SACX_USER_WINDOW_WRITE,h,0x1000,((uint64_t)1<<32)|2048)==-22);
 assert(!call(1,SACX_USER_WINDOW_WRITE,h,0x1000,1024));assert(visible[0]==0xffffffff);
 assert(call(1,SACX_USER_WINDOW_PRESENT,h,0,0)==-11);
 assert(!call(1,SACX_USER_WINDOW_WRITE,h,0x1000,((uint64_t)1024<<32)|1024));assert(!call(1,SACX_USER_WINDOW_PRESENT,h,0,0));assert(presented==1 && visible[0]==0xff123456 && visible[2047]==0xff123456);
 sacx_user_window_event e={.type=SACX_WINDOW_DOWN,.buttons=1,.x=7,.y=9};assert(!dihos_user_window_push(h,&e));e.type=SACX_WINDOW_UP;e.buttons=0;assert(!dihos_user_window_push(h,&e));
 assert(call(1,SACX_USER_WINDOW_EVENT,h,0xffffffffffffffffull,sizeof(e))==-14);
 assert(!call(1,SACX_USER_WINDOW_EVENT,h,0x1000,sizeof(e)));memcpy(&e,user,sizeof(e));assert(e.type==SACX_WINDOW_DOWN && e.x==7 && e.y==9);
 assert(!call(1,SACX_USER_WINDOW_EVENT,h,0x1000,sizeof(e)));memcpy(&e,user,sizeof(e));assert(e.type==SACX_WINDOW_UP && !e.buttons);
 e=(sacx_user_window_event){.type=SACX_WINDOW_KEY_DOWN,.x=4,.y='a'};assert(!dihos_user_window_push(h,&e));
 e.type=SACX_WINDOW_KEY_UP;e.y=0;assert(!dihos_user_window_push(h,&e));
 assert(!call(1,SACX_USER_WINDOW_EVENT,h,0x1000,sizeof(e)));memcpy(&e,user,sizeof(e));assert(e.type==SACX_WINDOW_KEY_DOWN && e.x==4 && e.y=='a');
 assert(!call(1,SACX_USER_WINDOW_EVENT,h,0x1000,sizeof(e)));memcpy(&e,user,sizeof(e));assert(e.type==SACX_WINDOW_KEY_UP);
 e.y=0xd800;assert(dihos_user_window_push(h,&e)==-22);e.y=0;e.x=256;assert(dihos_user_window_push(h,&e)==-22);
 e.x=4;e.delta=32;assert(dihos_user_window_push(h,&e)==-22);e.delta=0;
 e.type=SACX_WINDOW_DOWN;for(unsigned i=0;i<65;++i)assert(!dihos_user_window_push(h,&e));
 assert(!call(1,SACX_USER_WINDOW_EVENT,h,0x1000,sizeof(e)));memcpy(&e,user,sizeof(e));assert(e.type==SACX_WINDOW_RESET);
 assert(!call(1,SACX_USER_WINDOW_EVENT,h,0x1000,sizeof(e)));assert(call(1,SACX_USER_WINDOW_EVENT,h,0x1000,sizeof(e))==-11);
 dihos_user_window_cleanup(2);assert(allocations==2);dihos_user_window_cleanup(1);assert(!allocations && destroyed==1);
 assert(call(1,SACX_USER_WINDOW_CLOSE,h,0,0)==-9);int64_t next=create(1);assert(next>h);assert(call(1,SACX_USER_WINDOW_CLOSE,h,0,0)==-9);assert(!call(1,SACX_USER_WINDOW_CLOSE,next,0,0));assert(!allocations);
 sacx_user_window_desc big={256,128,"Batched upload"};memcpy(user,&big,sizeof(big));h=call(1,SACX_USER_WINDOW_CREATE,0x1000,sizeof(big),0);assert(h>0);
 for(unsigned i=0;i<32768;++i)((uint32_t*)user)[i]=0xffabcdef;
 protected_page=0x8000;unsigned before=copies;
 assert(call(1,SACX_USER_WINDOW_WRITE,h,0x1000,16384)==-14 && copies==before && visible[0]==0xffffffff);
 protected_page=0;assert(!call(1,SACX_USER_WINDOW_WRITE,h,0x1000,16384) && copies==before+16);
 assert(call(1,SACX_USER_WINDOW_PRESENT,h,0,0)==-11);
 assert(!call(1,SACX_USER_WINDOW_WRITE,h,0x1000,((uint64_t)16384<<32)|16384));assert(!call(1,SACX_USER_WINDOW_PRESENT,h,0,0));
 assert(visible[0]==0xffabcdef && visible[32767]==0xffabcdef);assert(!call(1,SACX_USER_WINDOW_CLOSE,h,0,0) && !allocations);
 h=create(1);assert(h>0);
 sacx_user_window_region r={62,30,2,2};memcpy(user,&r,sizeof(r));
 assert(call(2,SACX_USER_WINDOW_REGION,h,0x1000,sizeof(r))==-9);
 assert(!call(1,SACX_USER_WINDOW_REGION,h,0x1000,sizeof(r)));
 assert(call(1,SACX_USER_WINDOW_REGION,h,0x1000,sizeof(r))==-16);
 uint32_t packed[]={1,2,3,4};memcpy(user,packed,sizeof(packed));
 assert(call(1,SACX_USER_WINDOW_WRITE,h,0x1000,((uint64_t)1<<32)|4)==-22);
 assert(!call(1,SACX_USER_WINDOW_WRITE,h,0x1000,2));
 assert(visible[1982]==0xffffffff && call(1,SACX_USER_WINDOW_PRESENT,h,0,0)==-11);
 assert(!call(1,SACX_USER_WINDOW_WRITE,h,0x1008,((uint64_t)2<<32)|2));
 assert(!call(1,SACX_USER_WINDOW_PRESENT,h,0,0));
 assert(visible[1982]==1 && visible[1983]==2 && visible[2046]==3 && visible[2047]==4 && visible[1981]==0xffffffff);
 r=(sacx_user_window_region){63,0,2,1};memcpy(user,&r,sizeof(r));assert(call(1,SACX_USER_WINDOW_REGION,h,0x1000,sizeof(r))==-22);
 r=(sacx_user_window_region){0,0,64,32};memcpy(user,&r,sizeof(r));assert(!call(1,SACX_USER_WINDOW_REGION,h,0x1000,sizeof(r)));
 protected_page=0x2000;before=copies;
 assert(call(1,SACX_USER_WINDOW_WRITE,h,0x1000,2048)==-14 && copies==before && visible[1982]==1);
 protected_page=0;for(unsigned i=0;i<2048;++i)((uint32_t*)user)[i]=0xff223344;
 assert(!call(1,SACX_USER_WINDOW_WRITE,h,0x1000,2048));assert(!call(1,SACX_USER_WINDOW_PRESENT,h,0,0));
 assert(visible[0]==0xff223344 && visible[2047]==0xff223344);
 for(unsigned i=0;i<2048;++i)((uint32_t*)user)[i]=0xff556677;
 assert(!call(1,SACX_USER_WINDOW_WRITE,h,0x1000,2048));assert(!call(1,SACX_USER_WINDOW_PRESENT,h,0,0));
 assert(visible[0]==0xff556677 && visible[2047]==0xff556677);
 assert(!call(1,SACX_USER_WINDOW_CLOSE,h,0,0) && !allocations);
 puts("PASS: owned windows, capability-independent owner checks, generation reuse, bounded atomic frames, protected buffers, ordered clicks, overflow reset and OOM/exit cleanup");
 puts("PASS: 64 KiB uploads use 16 bounded page copies, reject a protected later page before mutation and keep presentation atomic");
 puts("PASS: packed damage preserves surrounding pixels, rejects invalid owners/bounds/protected pages and interoperates with full frames");
}
