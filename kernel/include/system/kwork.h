#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef void (*kwork_fn)(void *ctx);

enum
{
    KWORK_SUBMIT_ANY = 0u,
    KWORK_SUBMIT_REQUIRE_REMOTE = 1u << 0,
    KWORK_SUBMIT_QUIET = 1u << 1,
};

enum
{
    KWORK_STATUS_EMPTY = 0u,
    KWORK_STATUS_QUEUED = 1u,
    KWORK_STATUS_RUNNING = 2u,
    KWORK_STATUS_DONE = 3u,
    KWORK_STATUS_FAILED = 4u,
};

typedef struct kwork_stats
{
    uint32_t initialized;
    uint32_t queue_capacity;
    uint32_t queued;
    uint32_t running;
    uint32_t submitted_total;
    uint32_t claimed_total;
    uint32_t completed_total;
    uint32_t failed_total;
    uint32_t cancelled_total;
    uint32_t last_submitted_job;
    uint32_t last_claimed_job;
    uint32_t last_claimed_core;
    uint32_t last_cancelled_job;
    uint32_t workers_online;
    uint32_t workers_busy;
} kwork_stats;

void kwork_init(void);
int kwork_submit(kwork_fn fn, void *ctx, uint32_t flags, uint32_t *out_job_id);
uint32_t kwork_status(uint32_t job_id);
int kwork_cancel(uint32_t job_id);
int kwork_wait(uint32_t job_id, uint64_t spin_limit);
void kwork_get_stats(kwork_stats *out_stats);

#ifdef __cplusplus
}
#endif
