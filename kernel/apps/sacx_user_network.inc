/* Core-0 EL0 broker. The existing continuation remains the transport owner. */
#include "process/dihos_user_network.h"
#include "net/http_request_headers.h"
#define USER_HTTP_SLOTS 16u
/* Four owned slots bound a process to 64 MiB; reads remain checked 4 KiB
 * copies. Modern application scripts can exceed the former 8 MiB bound. */
#define USER_HTTP_CAPACITY SACX_HTTP_RESPONSE_MAX
typedef struct user_http_slot {sacx_net_request_slot request;uint64_t owner,generation;uint32_t consumed;} user_http_slot;
static user_http_slot G_user_http[USER_HTTP_SLOTS];
static int user_http_prefix(const char *text,const char *prefix){while(*prefix)if(*text++!=*prefix++)return 0;return 1;}
static int user_http_unverified(const sacx_net_request_slot *r){
    return r->info.tls_unverified || (!r->tls_verified &&
        (r->tls_required || user_http_prefix(r->url,"https://") || user_http_prefix(r->info.final_url,"https://")));
}
static user_http_slot *user_http_find(uint64_t owner,uint64_t id){
    unsigned index=(unsigned)(id&255);if(!owner || !index || index>USER_HTTP_SLOTS)return 0;
    user_http_slot *s=&G_user_http[index-1];
    return s->owner==owner && s->request.used && (id>>8)==s->generation?s:0;
}
static void user_http_close(user_http_slot *s){
    s->owner=0;s->consumed=0;
    if(&s->request==G_net_active_slot){s->request.cancelled=1;s->request.release_when_done=1;return;}
    if(s->request.buffer)pmem_free_pages(s->request.buffer,s->request.pages);
    memset(&s->request,0,sizeof(s->request));
}
void dihos_user_network_cleanup(uint64_t owner){
    if(owner)for(unsigned i=0;i<USER_HTTP_SLOTS;++i)if(G_user_http[i].owner==owner)user_http_close(&G_user_http[i]);
}
int64_t dihos_user_network_call(const aarch64_user_vm *vm,uint64_t owner,uint64_t op,const uint64_t a[3]){
    if(!owner)return -1;
    if(op==SACX_USER_HTTP_START || op==SACX_USER_HTTP_START_MANUAL || op==SACX_USER_HTTP_REQUEST || op==SACX_USER_HTTP_SEND){
        uint64_t url_address=a[0],url_bytes=a[1],timeout=a[2];
        uint64_t body_address=0;uint32_t body_bytes=0,method=SACX_USER_HTTP_GET;
        char headers[SACX_USER_HTTP_REQUEST_HEADERS_MAX+1];uint32_t header_bytes=0;
        if(op==SACX_USER_HTTP_SEND){
            if(a[1]!=sizeof(sacx_user_http_send) || a[2])return -22;
            sacx_user_http_send desc;int rc=aarch64_user_vm_copy_in(vm,&desc,a[0],sizeof(desc));if(rc)return rc;
            if(desc.reserved || (desc.method!=SACX_USER_HTTP_GET && desc.method!=SACX_USER_HTTP_POST) ||
               desc.body_bytes>SACX_USER_HTTP_BODY_MAX || (desc.method==SACX_USER_HTTP_GET && desc.body_bytes) ||
               (!desc.body_bytes && desc.body) || desc.header_bytes>SACX_USER_HTTP_REQUEST_HEADERS_MAX || (!desc.header_bytes && desc.headers))return -22;
            header_bytes=desc.header_bytes;
            if(header_bytes){rc=aarch64_user_vm_copy_in(vm,headers,desc.headers,header_bytes);if(rc)return rc;}
            rc=dihos_http_validate_request_headers(headers,header_bytes);if(rc)return rc;
            url_address=desc.url;url_bytes=desc.url_bytes;timeout=desc.timeout_ms;method=desc.method;body_address=desc.body;body_bytes=desc.body_bytes;
            if(body_bytes){rc=aarch64_user_vm_check_buffer(vm,body_address,body_bytes,0);if(rc)return rc;}
        }
        if(op==SACX_USER_HTTP_REQUEST){
            if(a[1]!=sizeof(sacx_user_http_request) || a[2])return -22;
            sacx_user_http_request desc;int rc=aarch64_user_vm_copy_in(vm,&desc,a[0],sizeof(desc));if(rc)return rc;
            if(desc.reserved || desc.header_bytes>SACX_USER_HTTP_REQUEST_HEADERS_MAX || (!desc.header_bytes && desc.headers))return -22;
            header_bytes=desc.header_bytes;
            if(header_bytes){rc=aarch64_user_vm_copy_in(vm,headers,desc.headers,header_bytes);if(rc)return rc;}
            rc=dihos_http_validate_request_headers(headers,header_bytes);if(rc)return rc;
            url_address=desc.url;url_bytes=desc.url_bytes;timeout=desc.timeout_ms;
        }
        if(!url_bytes || url_bytes>SACX_USER_HTTP_URL_MAX || timeout<1000 || timeout>120000)return -22;
        char url[SACX_USER_HTTP_URL_MAX+1];
        int rc=aarch64_user_vm_copy_in(vm,url,url_address,url_bytes);if(rc)return rc;url[url_bytes]=0;
        for(unsigned i=0;i<url_bytes;++i)if((unsigned char)url[i]<=32 || (unsigned char)url[i]>=127)return -22;
        if(!user_http_prefix(url,"http://") && !user_http_prefix(url,"https://"))return -22;
        unsigned owned=0,index=USER_HTTP_SLOTS;
        for(unsigned i=0;i<USER_HTTP_SLOTS;++i){
            if(G_user_http[i].owner==owner)++owned;
            if(index==USER_HTTP_SLOTS && !G_user_http[i].request.used && G_user_http[i].generation<0x7fffffffffffffull)index=i;
        }
        if(owned>=4 || index==USER_HTTP_SLOTS)return -24;
        user_http_slot *s=&G_user_http[index];
        uint8_t *buffer=(uint8_t*)pmem_alloc_pages(USER_HTTP_CAPACITY/4096);if(!buffer)return -12;
        memset(&s->request,0,sizeof(s->request));s->request.buffer=buffer;s->request.pages=USER_HTTP_CAPACITY/4096;
        s->request.capacity=USER_HTTP_CAPACITY;s->request.timeout_ms=(uint32_t)timeout;s->request.redirect_limit=5;
        s->request.no_follow=op!=SACX_USER_HTTP_START;
        memcpy(s->request.url,url,url_bytes+1);memcpy(s->request.method,method==SACX_USER_HTTP_POST?"POST":"GET",method==SACX_USER_HTTP_POST?5:4);
        if(body_bytes){
            rc=aarch64_user_vm_copy_in(vm,s->request.request_body,body_address,body_bytes);
            if(rc){pmem_free_pages(buffer,USER_HTTP_CAPACITY/4096);memset(&s->request,0,sizeof(s->request));return rc;}
        }
        s->request.request_body_size=body_bytes;
        if(header_bytes)memcpy(s->request.request_headers,headers,header_bytes);
        s->request.request_headers[header_bytes]=0;s->request.request_header_bytes=header_bytes;
        s->request.status=SACX_NET_STATUS_QUEUED;s->request.used=1;s->consumed=0;++s->generation;s->owner=owner;
        return (int64_t)((s->generation<<8)|(index+1));
    }
    user_http_slot *s=user_http_find(owner,a[0]);if(!s)return -9;
    sacx_net_request_slot *r=&s->request;
    if(op==SACX_USER_HTTP_CLOSE){if(a[1] || a[2])return -22;user_http_close(s);return 0;}
    if(op==SACX_USER_HTTP_HEADERS){
        if(!r->no_follow)return -95; // A followed request can replace its header buffer.
        uint32_t bytes=(uint32_t)a[2],offset=(uint32_t)(a[2]>>32);
        if(!bytes || bytes>SACX_USER_HTTP_READ_MAX)return -22;
        int rc=aarch64_user_vm_check_buffer(vm,a[1],bytes,1);if(rc)return rc;
        if(r->stream.failed || r->status==SACX_NET_STATUS_FAILED)return -5;
        if(r->status==SACX_NET_STATUS_CANCELLED)return -125;
        if(!r->stream.headers_ready)return r->status==SACX_NET_STATUS_DONE?-71:-11;
        if(user_http_unverified(r))return -13;
        uint32_t length=r->stream.header_bytes;
        if(length>sizeof(r->stream.headers))return -5;
        if(offset>length)return -22;
        uint32_t count=length-offset;if(count>bytes)count=bytes;
        if(!count)return 0;
        rc=aarch64_user_vm_copy_out(vm,a[1],r->stream.headers+offset,count);
        return rc?rc:(int64_t)count;
    }
    if(op==SACX_USER_HTTP_POLL){
        if(a[2]!=sizeof(sacx_user_http_info))return -22;
        sacx_user_http_info info={0};info.state=r->status;info.http_status=r->info.http_status;
        info.received_bytes=r->body_size;info.available_bytes=r->body_size>=s->consumed?r->body_size-s->consumed:0;
        info.truncated=r->truncated || r->info.truncated || r->stream.truncated;
        // Pending HTTPS is not yet authenticated, but exposes no response.
        // Never infer trust just from the scheme or a successful TCP connection.
        int https=r->tls_required || user_http_prefix(r->url,"https://") || user_http_prefix(r->info.final_url,"https://");
        info.tls_unverified=r->info.tls_unverified || (https && !r->tls_verified && (r->body_size || r->stream.headers_ready || r->info.http_status));
        memcpy(info.content_type,r->info.content_type,sizeof(info.content_type));memcpy(info.error,r->info.error,sizeof(info.error));
        return aarch64_user_vm_copy_out(vm,a[1],&info,sizeof(info));
    }
    if(op==SACX_USER_HTTP_READ){
        if(!a[2] || a[2]>SACX_USER_HTTP_READ_MAX)return -22;
        int rc=aarch64_user_vm_check_buffer(vm,a[1],a[2],1);if(rc)return rc;
        if(r->body_size>r->capacity || s->consumed>r->body_size)return -5;
        if((r->body_size || r->stream.headers_ready) && user_http_unverified(r))return -13;
        uint32_t bytes=r->body_size-s->consumed;if(bytes>a[2])bytes=(uint32_t)a[2];
        if(bytes){rc=aarch64_user_vm_copy_out(vm,a[1],r->buffer+s->consumed,bytes);if(rc)return rc;s->consumed+=bytes;return bytes;}
        if(r->status==SACX_NET_STATUS_FAILED)return -5;
        if(r->status==SACX_NET_STATUS_CANCELLED)return -125;
        if(r->status==SACX_NET_STATUS_DONE)return (r->truncated || r->info.truncated || r->stream.truncated)?-75:0;
        return -11;
    }
    return -38;
}
