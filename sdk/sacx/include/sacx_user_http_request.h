#pragma once
#ifndef SACX_USER_ABI_VERSION
#include "sacx_user_syscall.h"
#endif

/* Append-only ABI: a manual-redirect GET with owned URL/header copies. */
#define SACX_USER_HTTP_REQUEST 38u
#include "sacx_http_limits.h"
#define SACX_USER_HTTP_REQUEST_HEADERS_MAX SACX_HTTP_REQUEST_HEADERS_MAX
typedef struct sacx_user_http_request {
    uint64_t url, headers;
    uint32_t url_bytes, header_bytes, timeout_ms, reserved;
} sacx_user_http_request;
/* Append-only extension; GET/POST data is copied before admission returns.
 * No file handles, raw packet operations or redirects are granted here. */
#define SACX_USER_HTTP_SEND 40u
#define SACX_USER_HTTP_BODY_MAX SACX_HTTP_REQUEST_BODY_MAX
#define SACX_USER_HTTP_GET 1u
#define SACX_USER_HTTP_POST 2u
typedef struct sacx_user_http_send {
    uint64_t url,headers,body;
    uint32_t url_bytes,header_bytes,body_bytes,timeout_ms,method,reserved;
} sacx_user_http_send;
#if defined(__aarch64__)
static inline int64_t sacx_user_http_send_request(const char* url,uint32_t bytes,const char* headers,uint32_t header_bytes,const void* body,uint32_t body_bytes,uint32_t method,uint32_t timeout_ms)
{
    sacx_user_http_send request={(uintptr_t)url,header_bytes?(uintptr_t)headers:0,body_bytes?(uintptr_t)body:0,bytes,header_bytes,body_bytes,timeout_ms,method,0};
    return sacx_user_call(SACX_USER_HTTP_SEND,(uintptr_t)&request,sizeof(request),0);
}
static inline int64_t sacx_user_http_start_request(const char *url,uint32_t bytes,
    const char *headers,uint32_t header_bytes,uint32_t timeout_ms)
{
    sacx_user_http_request request={(uintptr_t)url,(uintptr_t)headers,bytes,header_bytes,timeout_ms,0};
    return sacx_user_call(SACX_USER_HTTP_REQUEST,(uintptr_t)&request,sizeof(request),0);
}
#endif
