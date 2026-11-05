/* Existing SACX frontend owns all browser chrome. Its child receives a
   broker-owned image clipped to that frontend's existing page viewport. */
static struct {
 uint64_t owner,window;uint32_t session,loading;
 kgfx_obj_handle parent,image;
 dihos_process_handle child;
} G_browser_frontend;
static void sacx_browser_sync(void){
 auto& b=G_browser_frontend;if(!b.owner)return;
 if(b.loading){
  dihos_user_load_status loaded={};int rc=dihos_user_load_browser_result(b.owner,&loaded);
  if(rc==-11)return;
  b.loading=0;
  if(rc || loaded.state!=3){dihos_browser_set_state(b.owner,b.session,SACX_BROWSER_FAILED,"WebCore could not be loaded");return;}
  b.child=loaded.handle;
  (void)dihos_user_runtime_set_interactive(b.child,4000);
  (void)dihos_browser_bind(b.owner,b.session,((uint64_t)b.child.generation<<8)|(b.child.slot+1u));
 }
 if(b.child.generation){
  dihos_user_task_info tasks[DIHOS_USER_TASKS];unsigned n=dihos_user_runtime_snapshot(tasks,DIHOS_USER_TASKS);bool found=false;
  for(unsigned i=0;i<n;++i)if(tasks[i].handle.slot==b.child.slot && tasks[i].handle.generation==b.child.generation){
   found=true;
   if(tasks[i].state==DIHOS_USER_FAILED)dihos_browser_set_state(b.owner,b.session,SACX_BROWSER_EXITED,"WebCore renderer faulted; press Reload to restart");
   else if(tasks[i].state==DIHOS_USER_DONE || tasks[i].state==DIHOS_USER_STOPPED)dihos_browser_set_state(b.owner,b.session,SACX_BROWSER_EXITED,"WebCore renderer stopped");
  }
  if(!found)dihos_browser_set_state(b.owner,b.session,SACX_BROWSER_EXITED,"WebCore renderer is no longer running");
 }
}
static void sacx_browser_close_task(uint64_t owner){
 auto& b=G_browser_frontend;if(!owner || b.owner!=owner)return;
 if(b.loading)dihos_user_load_browser_cancel(owner);
 else if(b.child.generation)(void)dihos_user_runtime_stop(b.child);
 if(b.image.idx>=0 && kgfx_obj_ref(b.image))kgfx_obj_destroy(b.image);
 (void)dihos_browser_close(owner,b.session);b={};b.image.idx=-1;
}
extern "C" int sacx_browser_surface_create(uint64_t window,const sacx_user_window_desc* d,const uint32_t* pixels){
 auto& b=G_browser_frontend;sacx_browser_sync();
 if(!b.owner || dihos_user_window_owner(window)!=dihos_browser_renderer(b.owner,b.session))return -38;
 if(b.window || !kgfx_obj_ref(b.parent))return -9;
 auto image=kgfx_obj_add_image(pixels,d->width,d->height,0,0,d->width);if(image.idx<0)return -12;
 kgfx_obj_set_parent(image,b.parent);kgfx_obj_set_clip_to_parent(image,1);b.image=image;b.window=window;return 0;
}
extern "C" int sacx_browser_surface_present(uint64_t window,const uint32_t* pixels){
 auto& b=G_browser_frontend;if(!b.owner || b.window!=window)return 0;
 kgfx_obj* image=kgfx_obj_ref(b.image);if(image){image->u.image.argb=pixels;kgfx_image_touch(b.image);}return 1;
}
extern "C" int sacx_browser_surface_destroy(uint64_t window){
 auto& b=G_browser_frontend;if(!b.owner || b.window!=window)return 0;
 if(b.image.idx>=0 && kgfx_obj_ref(b.image))kgfx_obj_destroy(b.image);
 b.image.idx=-1;b.window=0;return 1;
}
static int sacx_api_browser_session(uint32_t op,void* payload,uint32_t bytes){
 if(!G_current_task || !payload || smp_current_logical_id()!=0)return -1;
 uint64_t owner=G_current_task->task_id;auto& b=G_browser_frontend;
 if(op==SACX_BROWSER_OPEN){
  if(bytes!=sizeof(sacx_browser_open))return -22;
  auto* open=(sacx_browser_open*)payload;auto* parent=sacx_gfx_from_handle(G_current_task,open->parent);
  if(!parent || !kgfx_obj_ref(parent->handle))return -9;
  uint32_t session=0;int rc=dihos_browser_open(owner,open->width,open->height,&session);if(rc)return rc;
  rc=dihos_user_load_start_browser("0:/OS/System/Programs/Dihscover/WebCore.bin",owner);
  if(rc){dihos_browser_close(owner,session);return rc;}
  b={};b.owner=owner;b.session=session;b.parent=parent->handle;b.image.idx=-1;b.loading=1;open->session=session;return 0;
 }
 if(b.owner!=owner)return -9;
 sacx_browser_sync();
 if(op==SACX_BROWSER_SEND){if(bytes!=sizeof(sacx_browser_send))return -22;return dihos_browser_send(owner,(const sacx_browser_send*)payload);}
 if(op==SACX_BROWSER_STATUS){if(bytes!=sizeof(sacx_browser_info))return -22;auto* info=(sacx_browser_info*)payload;return dihos_browser_status(owner,info->session,info);}
 if(op==SACX_BROWSER_STARTUP_PROGRESS){
  if(bytes!=sizeof(sacx_browser_startup_progress))return -22;
  auto* progress=(sacx_browser_startup_progress*)payload;if(progress->session!=b.session)return -9;
  *progress={b.session,0,0,0};
  if(b.loading){dihos_user_load_status loaded={};int rc=dihos_user_load_browser_result(owner,&loaded);
   if(rc!=-11)return rc?rc:-11;
   progress->state=loaded.state;progress->read_bytes=loaded.read_bytes;progress->total_bytes=loaded.total_bytes;}
  return 0;
 }
 if(op==SACX_BROWSER_CLOSE){if(bytes!=sizeof(uint32_t) || *(uint32_t*)payload!=b.session)return -9;sacx_browser_close_task(owner);return 0;}
 return -38;
}
