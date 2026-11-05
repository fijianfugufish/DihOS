#pragma once
#include "apps/sacx_api.h"
#include "net/http_stream.h"
typedef struct __attribute__((aligned(64))) sacx_net_request_slot
{
    uint8_t used;
    uint8_t truncated;
    uint8_t release_when_done,no_follow;
    uint8_t tls_required,tls_verified;
    uint32_t id;
    uint32_t job_id;
    volatile uint32_t status;
    volatile uint32_t cancelled;
    uint32_t capacity;
    uint32_t redirect_limit;
    uint32_t timeout_ms;
    uint32_t raw_size;
    uint32_t body_size;
    uint64_t pages;
    uint8_t *buffer;
    sacx_net_response_info info;
    http_stream stream;
    char url[SACX_HTTP_URL_MAX+1];
    char method[8];
    char content_type[96];
    uint32_t request_header_bytes;
    char request_headers[SACX_HTTP_REQUEST_HEADERS_MAX+1];
    uint32_t request_body_size;
    uint8_t request_body[SACX_HTTP_REQUEST_BODY_MAX];
} sacx_net_request_slot;
