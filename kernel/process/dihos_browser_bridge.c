#include "process/dihos_browser_bridge.h"
/* One live browser session for now. Tokens never identify kernel addresses. */
static struct {uint64_t frontend,renderer;uint32_t generation,head,count,reset;
 sacx_browser_info info;sacx_browser_command queue[SACX_BROWSER_QUEUE];} browser;
static int owned(uint64_t owner,uint32_t token){return owner && browser.frontend==owner && token==browser.generation;}
static int terminated(const char* s,unsigned n){for(unsigned i=0;i<n;++i)if(!s[i])return 1;return 0;}
static int valid(const sacx_browser_command* c){
 if(!c || c->type<SACX_BROWSER_RESIZE || c->type>SACX_BROWSER_DIAGNOSTICS)return 0;
 if(c->type==SACX_BROWSER_RESIZE)return c->a>0 && c->b>0 && c->a<=SACX_BROWSER_MAX_WIDTH && c->b<=SACX_BROWSER_MAX_HEIGHT;
 if(c->type==SACX_BROWSER_NAVIGATE){
  if(!c->text[0])return 0;
  for(unsigned i=0;i<sizeof(c->text);++i){unsigned char ch=c->text[i];if(!ch)return 1;if(ch<32 || ch==127)return 0;}return 0;
 }
 if(c->type==SACX_BROWSER_PREFS)return c->a>=0 && c->a<3 && c->b>=6 && c->b<=32 && c->c>=50 && c->c<=300 &&
   c->d>=0 && !(c->d&~0xffff) && (c->d&255)<3 && ((c->d>>8)&255)<3;
 if(c->type==SACX_BROWSER_MOUSE)return c->c>=0 && !(c->c&~7) && c->d>=-120 && c->d<=120;
 if(c->type==SACX_BROWSER_KEY)return c->a>=4 && c->a<=255 && c->b>=0 && c->b<=0x10ffff &&
   !(c->b>=0xd800 && c->b<=0xdfff) && c->c>=0 && !(c->c&~31);
 return 1;
}
int dihos_browser_open(uint64_t owner,uint32_t w,uint32_t h,uint32_t* token){
 if(!owner || !token || !w || !h || w>SACX_BROWSER_MAX_WIDTH || h>SACX_BROWSER_MAX_HEIGHT)return -22;
 if(browser.frontend)return -16;
 if(browser.generation==0xffffffffu)return -75;
 uint32_t next=browser.generation+1;browser=(__typeof__(browser)){0};browser.generation=next;
 browser.frontend=owner;browser.info=(sacx_browser_info){.session=next,.state=SACX_BROWSER_STARTING,.width=w,.height=h};
 *token=next;return 0;
}
int dihos_browser_bind(uint64_t owner,uint32_t token,uint64_t renderer){
 if(!owned(owner,token) || !renderer)return -9;
 if(browser.renderer && browser.renderer!=renderer)return -16;
 browser.renderer=renderer;return 0;
}
uint64_t dihos_browser_renderer(uint64_t owner,uint32_t token){return owned(owner,token)?browser.renderer:0;}
int dihos_browser_send(uint64_t owner,const sacx_browser_send* send){
 if(!send || !owned(owner,send->session))return -9;
 if(send->reserved || !valid(&send->command))return -22;
 const sacx_browser_command* c=&send->command;
 if(browser.count){
  unsigned tail=(browser.head+browser.count-1)%SACX_BROWSER_QUEUE;sacx_browser_command* last=&browser.queue[tail];
  if(last->type==c->type && (c->type==SACX_BROWSER_RESIZE || c->type==SACX_BROWSER_PREFS ||
    (c->type==SACX_BROWSER_MOUSE && last->c==c->c && !last->d && !c->d))){*last=*c;return 0;}
 }
 if(browser.count==SACX_BROWSER_QUEUE){browser.reset=1;return -11;}
 browser.queue[(browser.head+browser.count)%SACX_BROWSER_QUEUE]=*c;++browser.count;return 0;
}
int dihos_browser_status(uint64_t owner,uint32_t token,sacx_browser_info* out){
 if(!out || !owned(owner,token))return -9;*out=browser.info;return 0;
}
int dihos_browser_set_state(uint64_t owner,uint32_t token,uint32_t state,const char* message){
 if(!owned(owner,token) || state<SACX_BROWSER_STARTING || state>SACX_BROWSER_EXITED)return -9;
 browser.info.state=state;unsigned i=0;
 for(;message && message[i] && i+1<sizeof(browser.info.error);++i)browser.info.error[i]=message[i];
 browser.info.error[i]=0;return 0;
}
int dihos_browser_close(uint64_t owner,uint32_t token){
 if(!owned(owner,token))return -9;
 uint32_t generation=browser.generation;browser=(__typeof__(browser)){0};browser.generation=generation;return 0;
}
int dihos_browser_native_call(const aarch64_user_vm* vm,uint64_t renderer,uint32_t op,uint64_t buffer,uint32_t bytes){
 if(!renderer || !browser.frontend || renderer!=browser.renderer)return -9;
 if(op==SACX_BROWSER_DESCRIBE){
  if(bytes!=sizeof(sacx_browser_info))return -22;
  return aarch64_user_vm_copy_out(vm,buffer,&browser.info,bytes);
 }
 if(op==SACX_BROWSER_RECEIVE){
  if(bytes!=sizeof(sacx_browser_command))return -22;
  int rc=aarch64_user_vm_check_buffer(vm,buffer,bytes,1);if(rc)return rc;
  if(!browser.count){
   if(!browser.reset)return -11;
   sacx_browser_command reset={.type=SACX_BROWSER_RESET};rc=aarch64_user_vm_copy_out(vm,buffer,&reset,bytes);
   if(!rc)browser.reset=0;return rc;
  }
  rc=aarch64_user_vm_copy_out(vm,buffer,&browser.queue[browser.head],bytes);
  if(!rc){browser.head=(browser.head+1)%SACX_BROWSER_QUEUE;--browser.count;}return rc;
 }
 if(op==SACX_BROWSER_REPORT){
  if(bytes!=sizeof(sacx_browser_info))return -22;
  sacx_browser_info info;int rc=aarch64_user_vm_copy_in(vm,&info,buffer,bytes);if(rc)return rc;
  if(info.session!=browser.generation || info.reserved || info.state<SACX_BROWSER_STARTING || info.state>SACX_BROWSER_EXITED ||
    !info.width || !info.height || info.width>SACX_BROWSER_MAX_WIDTH || info.height>SACX_BROWSER_MAX_HEIGHT || info.pointer>=12 ||
    !terminated(info.title,sizeof(info.title)) || !terminated(info.url,sizeof(info.url)) || !terminated(info.error,sizeof(info.error)))return -22;
  browser.info=info;return 0;
 }
 return -38;
}
