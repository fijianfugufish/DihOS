#pragma once
#include "apps/sacx_api.h"
#include "net/http_stream.h"
typedef struct __attribute__((aligned(64))) sacx_net_request_slot
{
    uint8_t used;
    uint8_t truncated;
    uint8_t release_when_done,no_follow;
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
    char url[768];
    char method[8];
    char content_type[96];
    uint32_t request_body_size;
    uint8_t request_body[4096];
} sacx_net_request_slot;
