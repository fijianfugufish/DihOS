#pragma once
#include "sacx_user_syscall.h"
#include "sacx_http_limits.h"
#define SACX_USER_CAP_NETWORK (1ull<<9)
#define SACX_USER_HTTP_URL_MAX SACX_HTTP_URL_MAX
#define SACX_USER_HTTP_READ_MAX 4096u
#define SACX_USER_HTTP_HEADERS_MAX 16384u
enum { SACX_USER_HTTP_START=27,SACX_USER_HTTP_POLL=28,SACX_USER_HTTP_READ=29,SACX_USER_HTTP_CLOSE=30,
       SACX_USER_HTTP_HEADERS=31,SACX_USER_HTTP_START_MANUAL=32 };
enum { SACX_USER_HTTP_QUEUED=1,SACX_USER_HTTP_LOADING=2,SACX_USER_HTTP_DONE=3,SACX_USER_HTTP_FAILED=4,SACX_USER_HTTP_CANCELLED=5 };
typedef struct sacx_user_http_info {
    uint32_t state,http_status,available_bytes,received_bytes,truncated,tls_unverified;
    char content_type[96],error[128];
} sacx_user_http_info;
#if defined(__aarch64__)
// GET only; kernel owns a copy of URL and response. No implicit network grant.
static inline int64_t sacx_user_http_start(const char *url,uint32_t bytes,uint32_t timeout_ms){return sacx_user_call(SACX_USER_HTTP_START,(uintptr_t)url,bytes,timeout_ms);}
// Manual redirects expose the original response; WebCore owns redirect policy.
static inline int64_t sacx_user_http_start_manual(const char *url,uint32_t bytes,uint32_t timeout_ms){return sacx_user_call(SACX_USER_HTTP_START_MANUAL,(uintptr_t)url,bytes,timeout_ms);}
// Manual-mode response headers, including the status line. Non-consuming checked
// slices: <=4096 bytes per call, -11 until ready, 0 at end. No body offset changes.
static inline int sacx_user_http_headers(uint64_t id,void *out,uint32_t bytes,uint32_t offset){return (int)sacx_user_call(SACX_USER_HTTP_HEADERS,id,(uintptr_t)out,((uint64_t)offset<<32)|bytes);}
static inline int sacx_user_http_poll(uint64_t id,sacx_user_http_info *info){return (int)sacx_user_call(SACX_USER_HTTP_POLL,id,(uintptr_t)info,sizeof(*info));}
// Returns copied bytes, 0 at successful EOF, -11 while pending, or an error.
static inline int sacx_user_http_read(uint64_t id,void *out,uint32_t bytes){return (int)sacx_user_call(SACX_USER_HTTP_READ,id,(uintptr_t)out,bytes);}
static inline int sacx_user_http_close(uint64_t id){return (int)sacx_user_call(SACX_USER_HTTP_CLOSE,id,0,0);}
#endif
