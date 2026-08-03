#include "system/kwork.h"
#include "system/smp.h"
#include "asm/asm.h"
#include "terminal/terminal_api.h"
#include <stddef.h>
#include <stdint.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

enum
{
    KWORK_MAX_JOBS = 64u,
};

typedef struct
{
    volatile uint32_t status;
    uint32_t id;
    uint32_t flags;
    uint32_t owner_core;
    kwork_fn fn;
    void *ctx;
} kwork_job;

static kwork_job G_jobs[KWORK_MAX_JOBS] __attribute__((aligned(64)));
static volatile uint32_t G_lock __attribute__((aligned(64)));
static uint32_t G_next_job_id = 1u;
static uint32_t G_initialized;
static uint32_t G_submitted_total;
static uint32_t G_claimed_total;
static uint32_t G_completed_total;
static uint32_t G_failed_total;
static uint32_t G_cancelled_total;
static uint32_t G_last_submitted_job;
static uint32_t G_last_claimed_job;
static uint32_t G_last_claimed_core;
static uint32_t G_last_cancelled_job;
static uint32_t G_next_worker_assign;
static volatile uint32_t G_selftest_counter;

static void kwork_lock(void)
{
    while (__atomic_exchange_n(&G_lock, 1u, __ATOMIC_ACQUIRE))
        asm_relax();
}

static void kwork_unlock(void)
{
    __atomic_store_n(&G_lock, 0u, __ATOMIC_RELEASE);
}

static void kwork_log_uint(const char *label, uint64_t value)
{
    terminal_print(label);
    terminal_print_inline_hex64(value);
    terminal_flush_log();
}

static void kwork_log_text_uint_uint(const char *label, uint64_t a, const char *mid, uint64_t b)
{
    terminal_print(label);
    terminal_print_inline_hex64(a);
    terminal_print_inline(mid);
    terminal_print_inline_hex64(b);
    terminal_flush_log();
}

static void kwork_selftest_fn(void *ctx)
{
    volatile uint32_t *counter = (volatile uint32_t *)ctx;
    if (counter)
        *counter = *counter + 1u;
}

static kwork_job *kwork_find_locked(uint32_t job_id)
{
    for (uint32_t i = 0u; i < KWORK_MAX_JOBS; ++i)
        if (G_jobs[i].id == job_id && G_jobs[i].status != KWORK_STATUS_EMPTY)
            return &G_jobs[i];
    return NULL;
}

static kwork_job *kwork_claim_job(uint32_t logical_id)
{
    kwork_job *claimed = NULL;
    asm_dma_invalidate_range(G_jobs, sizeof(G_jobs));
    for (uint32_t i = 0u; i < KWORK_MAX_JOBS; ++i)
    {
        if (G_jobs[i].status == KWORK_STATUS_QUEUED &&
            G_jobs[i].owner_core == logical_id)
        {
            G_jobs[i].status = KWORK_STATUS_RUNNING;
            G_claimed_total = G_claimed_total + 1u;
            G_last_claimed_job = G_jobs[i].id;
            G_last_claimed_core = logical_id;
            asm_dma_clean_range(&G_jobs[i], sizeof(G_jobs[i]));
            claimed = &G_jobs[i];
            break;
        }
    }
    return claimed;
}

static uint32_t kwork_choose_worker(const smp_snapshot *smp)
{
    uint32_t worker_indices[SMP_MAX_CORES];
    uint32_t worker_count = 0u;

    if (!smp || !smp->worker_count)
        return 0u;
    for (uint32_t i = 0u; i < smp->core_count && i < SMP_MAX_CORES; ++i)
    {
        if (!smp->cores[i].enabled || smp->cores[i].state != SMP_CORE_ONLINE ||
            smp->cores[i].logical_id == 0u)
            continue;
        worker_indices[worker_count++] = smp->cores[i].logical_id;
    }
    if (!worker_count)
        return 0u;
    if (G_next_worker_assign >= worker_count)
        G_next_worker_assign = 0u;
    return worker_indices[G_next_worker_assign++ % worker_count];
}

static void kwork_worker_poll(uint32_t logical_id)
{
    kwork_job *job;
    for (;;)
    {
        job = kwork_claim_job(logical_id);
        if (!job)
            break;

        smp_mark_busy(logical_id, 1u);
        if (job->fn)
        {
            job->fn(job->ctx);
            job->status = KWORK_STATUS_DONE;
            G_completed_total = G_completed_total + 1u;
            smp_mark_job_done(logical_id);
            asm_dma_clean_range(job, sizeof(*job));
        }
        else
        {
            job->status = KWORK_STATUS_FAILED;
            G_failed_total = G_failed_total + 1u;
            asm_dma_clean_range(job, sizeof(*job));
        }
        smp_mark_busy(logical_id, 0u);
    }
}

void kwork_init(void)
{
    smp_snapshot smp;

    if (G_initialized)
        return;
    for (uint32_t i = 0u; i < KWORK_MAX_JOBS; ++i)
        G_jobs[i] = (kwork_job){0};
    G_next_job_id = 1u;
    G_submitted_total = 0u;
    G_claimed_total = 0u;
    G_completed_total = 0u;
    G_failed_total = 0u;
    G_cancelled_total = 0u;
    G_last_submitted_job = 0u;
    G_last_claimed_job = 0u;
    G_last_claimed_core = 0u;
    G_last_cancelled_job = 0u;
    G_next_worker_assign = 0u;
    G_initialized = 1u;
    smp_set_worker_poll(kwork_worker_poll);
    smp_get_snapshot(&smp);
    kwork_log_uint("[K:WORK] init workers=", smp.worker_count);
    if (smp.worker_count)
    {
        uint32_t selftest_job = 0u;
        G_selftest_counter = 0u;
        asm_dma_clean_range((const void *)&G_selftest_counter, sizeof(G_selftest_counter));
        if (kwork_submit(kwork_selftest_fn, (void *)&G_selftest_counter, KWORK_SUBMIT_REQUIRE_REMOTE, &selftest_job) == 0)
        {
            for (uint32_t spins = 0u; spins < 500000u; ++spins)
            {
                uint32_t status = kwork_status(selftest_job);
                if (status != KWORK_STATUS_QUEUED && status != KWORK_STATUS_RUNNING)
                    break;
                smp_signal_workers();
                asm_relax();
            }
            asm_dma_invalidate_range((const void *)&G_selftest_counter, sizeof(G_selftest_counter));
            kwork_log_text_uint_uint("[K:WORK] selftest status=", kwork_status(selftest_job),
                                     " counter=", G_selftest_counter);
            smp_get_snapshot(&smp);
            kwork_log_text_uint_uint("[K:WORK] selftest poll1=", smp.core_count > 1u ? smp.cores[1].poll_ticks : 0u,
                                     " stage1=", smp.core_count > 1u ? smp.cores[1].worker_stage : 0u);
            kwork_log_text_uint_uint("[K:WORK] selftest poll2=", smp.core_count > 2u ? smp.cores[2].poll_ticks : 0u,
                                     " stage2=", smp.core_count > 2u ? smp.cores[2].worker_stage : 0u);
        }
        else
        {
            kwork_log_uint("[K:WORK] selftest submit failed=", selftest_job);
        }
    }
}

int kwork_submit(kwork_fn fn, void *ctx, uint32_t flags, uint32_t *out_job_id)
{
    uint32_t id = 0u;
    uint32_t owner_core = 0u;
    smp_snapshot smp;

    if (out_job_id)
        *out_job_id = 0u;
    if (!G_initialized || !fn)
        return -1;
    smp_get_snapshot(&smp);
    if ((flags & KWORK_SUBMIT_REQUIRE_REMOTE) && !smp.worker_count)
        return -3;
    owner_core = kwork_choose_worker(&smp);
    if (!owner_core)
        return -3;

    kwork_lock();
    for (uint32_t i = 0u; i < KWORK_MAX_JOBS; ++i)
    {
        if (G_jobs[i].status == KWORK_STATUS_EMPTY ||
            G_jobs[i].status == KWORK_STATUS_DONE ||
            G_jobs[i].status == KWORK_STATUS_FAILED)
        {
            id = G_next_job_id++;
            if (!G_next_job_id)
                G_next_job_id = 1u;
            G_jobs[i].id = id;
            G_jobs[i].flags = flags;
            G_jobs[i].owner_core = owner_core;
            G_jobs[i].fn = fn;
            G_jobs[i].ctx = ctx;
            __atomic_store_n(&G_jobs[i].status, KWORK_STATUS_QUEUED, __ATOMIC_RELEASE);
            __atomic_fetch_add(&G_submitted_total, 1u, __ATOMIC_RELAXED);
            __atomic_store_n(&G_last_submitted_job, id, __ATOMIC_RELAXED);
            asm_dma_clean_range(&G_jobs[i], sizeof(G_jobs[i]));
            break;
        }
    }
    kwork_unlock();

    if (!id)
        return -2;
    if (out_job_id)
        *out_job_id = id;
    if (!(flags & KWORK_SUBMIT_QUIET))
        kwork_log_text_uint_uint("[K:WORK] submit job=", id, " owner=", owner_core);
    smp_signal_workers();
    return 0;
}

uint32_t kwork_status(uint32_t job_id)
{
    uint32_t status = KWORK_STATUS_EMPTY;
    asm_dma_invalidate_range(G_jobs, sizeof(G_jobs));
    for (uint32_t i = 0u; i < KWORK_MAX_JOBS; ++i)
    {
        if (G_jobs[i].id == job_id)
        {
            status = __atomic_load_n(&G_jobs[i].status, __ATOMIC_ACQUIRE);
            if (status != KWORK_STATUS_EMPTY)
                break;
        }
    }
    return status;
}

int kwork_cancel(uint32_t job_id)
{
    int rc = -1;
    asm_dma_invalidate_range(G_jobs, sizeof(G_jobs));
    for (uint32_t i = 0u; i < KWORK_MAX_JOBS; ++i)
    {
        if (G_jobs[i].id != job_id)
            continue;
        uint32_t status = __atomic_load_n(&G_jobs[i].status, __ATOMIC_ACQUIRE);
        if (status == KWORK_STATUS_EMPTY)
            continue;
        if (status == KWORK_STATUS_QUEUED)
        {
            uint32_t expected = KWORK_STATUS_QUEUED;
            if (__atomic_compare_exchange_n(&G_jobs[i].status, &expected, KWORK_STATUS_FAILED,
                                            0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            {
                G_jobs[i].fn = NULL;
                G_jobs[i].ctx = NULL;
                __atomic_fetch_add(&G_cancelled_total, 1u, __ATOMIC_RELAXED);
                __atomic_store_n(&G_last_cancelled_job, job_id, __ATOMIC_RELAXED);
                asm_dma_clean_range(&G_jobs[i], sizeof(G_jobs[i]));
                rc = 0;
            }
            else
            {
                rc = expected == KWORK_STATUS_RUNNING ? 1 : 0;
            }
        }
        else if (status == KWORK_STATUS_RUNNING)
        {
            rc = 1;
        }
        else
        {
            rc = 0;
        }
        break;
    }
    if (rc == 0)
    {
        smp_snapshot smp;
        smp_get_snapshot(&smp);
        kwork_log_uint("[K:WORK] cancel job=", job_id);
        kwork_log_text_uint_uint("[K:WORK] cancel diag claimed=", G_claimed_total,
                                 " last_core=", G_last_claimed_core);
        kwork_log_text_uint_uint("[K:WORK] cancel diag poll1=", smp.core_count > 1u ? smp.cores[1].poll_ticks : 0u,
                                 " poll2=", smp.core_count > 2u ? smp.cores[2].poll_ticks : 0u);
    }
    return rc;
}

int kwork_wait(uint32_t job_id, uint64_t spin_limit)
{
    uint64_t spins = 0u;
    for (;;)
    {
        uint32_t status = kwork_status(job_id);
        if (status == KWORK_STATUS_DONE)
            return 0;
        if (status == KWORK_STATUS_FAILED || status == KWORK_STATUS_EMPTY)
            return -1;
        if (spin_limit && spins++ >= spin_limit)
            return 1;
        asm_relax();
    }
}

void kwork_get_stats(kwork_stats *out_stats)
{
    smp_snapshot smp;
    if (!out_stats)
        return;
    *out_stats = (kwork_stats){0};
    out_stats->initialized = G_initialized;
    out_stats->queue_capacity = KWORK_MAX_JOBS;
    out_stats->submitted_total = G_submitted_total;
    out_stats->claimed_total = G_claimed_total;
    out_stats->completed_total = G_completed_total;
    out_stats->failed_total = G_failed_total;
    out_stats->cancelled_total = G_cancelled_total;
    out_stats->last_submitted_job = G_last_submitted_job;
    out_stats->last_claimed_job = G_last_claimed_job;
    out_stats->last_claimed_core = G_last_claimed_core;
    out_stats->last_cancelled_job = G_last_cancelled_job;

    kwork_lock();
    for (uint32_t i = 0u; i < KWORK_MAX_JOBS; ++i)
    {
        uint32_t status = G_jobs[i].status;
        if (status == KWORK_STATUS_QUEUED)
            ++out_stats->queued;
        else if (status == KWORK_STATUS_RUNNING)
            ++out_stats->running;
    }
    kwork_unlock();

    smp_get_snapshot(&smp);
    out_stats->workers_online = smp.worker_count;
    for (uint32_t i = 0u; i < smp.core_count; ++i)
        if (smp.cores[i].busy)
            ++out_stats->workers_busy;
}
