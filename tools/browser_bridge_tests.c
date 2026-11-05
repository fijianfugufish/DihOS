#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "process/dihos_browser_bridge.h"
static unsigned char user[4096];
int aarch64_user_vm_check_buffer(const aarch64_user_vm* vm,uint64_t p,uint64_t n,int write){(void)vm;(void)write;return p!=0x1000 || n>sizeof(user)?-14:0;}
int aarch64_user_vm_copy_out(const aarch64_user_vm* vm,uint64_t p,const void* src,uint64_t n){int rc=aarch64_user_vm_check_buffer(vm,p,n,1);if(!rc)memcpy(user,src,n);return rc;}
int aarch64_user_vm_copy_in(const aarch64_user_vm* vm,void* dst,uint64_t p,uint64_t n){int rc=aarch64_user_vm_check_buffer(vm,p,n,0);if(!rc)memcpy(dst,user,n);return rc;}
int main(void){
 uint32_t token;assert(!dihos_browser_open(1,960,568,&token));uint32_t old=token,other;
 assert(dihos_browser_open(2,960,568,&other)==-16);assert(dihos_browser_bind(2,token,257)==-9);assert(!dihos_browser_bind(1,token,257));
 sacx_browser_send send={.session=token,.command={.type=SACX_BROWSER_NAVIGATE}};strcpy(send.command.text,"https://example.com/");
 assert(dihos_browser_send(2,&send)==-9);assert(!dihos_browser_send(1,&send));
 assert(dihos_browser_native_call(0,258,SACX_BROWSER_RECEIVE,0x1000,sizeof(send.command))==-9);
 assert(dihos_browser_native_call(0,257,SACX_BROWSER_RECEIVE,0,sizeof(send.command))==-14);
 assert(!dihos_browser_native_call(0,257,SACX_BROWSER_RECEIVE,0x1000,sizeof(send.command)));
 sacx_browser_command got;memcpy(&got,user,sizeof(got));assert(got.type==SACX_BROWSER_NAVIGATE && !strcmp(got.text,send.command.text));
 send.command=(sacx_browser_command){.type=SACX_BROWSER_RESIZE,.a=10000,.b=1};assert(dihos_browser_send(1,&send)==-22);
 send.command=(sacx_browser_command){.type=SACX_BROWSER_MOUSE,.a=1,.b=2};assert(!dihos_browser_send(1,&send));send.command.a=7;assert(!dihos_browser_send(1,&send));
 send.command.c=1;assert(!dihos_browser_send(1,&send));send.command.c=0;assert(!dihos_browser_send(1,&send));
 assert(!dihos_browser_native_call(0,257,SACX_BROWSER_RECEIVE,0x1000,sizeof(got)));memcpy(&got,user,sizeof(got));assert(got.a==7 && got.c==0);
 assert(!dihos_browser_native_call(0,257,SACX_BROWSER_RECEIVE,0x1000,sizeof(got)));memcpy(&got,user,sizeof(got));assert(got.c==1);
 assert(!dihos_browser_native_call(0,257,SACX_BROWSER_RECEIVE,0x1000,sizeof(got)));memcpy(&got,user,sizeof(got));assert(!got.c);
 send.command.type=SACX_BROWSER_KEY;send.command.a=4;send.command.b='a';
 for(unsigned i=0;i<SACX_BROWSER_QUEUE;++i)assert(!dihos_browser_send(1,&send));assert(dihos_browser_send(1,&send)==-11);
 for(unsigned i=0;i<SACX_BROWSER_QUEUE;++i)assert(!dihos_browser_native_call(0,257,SACX_BROWSER_RECEIVE,0x1000,sizeof(got)));
 assert(!dihos_browser_native_call(0,257,SACX_BROWSER_RECEIVE,0x1000,sizeof(got)));memcpy(&got,user,sizeof(got));assert(got.type==SACX_BROWSER_RESET);
 assert(dihos_browser_native_call(0,257,SACX_BROWSER_RECEIVE,0x1000,sizeof(got))==-11);
 sacx_browser_info info;assert(!dihos_browser_status(1,token,&info));info.state=SACX_BROWSER_READY;strcpy(info.title,"Example Domain");memcpy(user,&info,sizeof(info));
 assert(!dihos_browser_native_call(0,257,SACX_BROWSER_REPORT,0x1000,sizeof(info)));assert(!dihos_browser_status(1,token,&info) && !strcmp(info.title,"Example Domain"));
 memset(info.title,'x',sizeof(info.title));memcpy(user,&info,sizeof(info));assert(dihos_browser_native_call(0,257,SACX_BROWSER_REPORT,0x1000,sizeof(info))==-22);
 assert(!dihos_browser_close(1,token));assert(dihos_browser_native_call(0,257,SACX_BROWSER_DESCRIBE,0x1000,sizeof(info))==-9);
 assert(!dihos_browser_open(2,640,480,&token) && token!=old);assert(dihos_browser_close(1,old)==-9);assert(!dihos_browser_close(2,token));
 puts("PASS: browser bridge owners, token generations, checked copies, commands, pointer ordering, coalescing, overflow reset and renderer reports");
}
