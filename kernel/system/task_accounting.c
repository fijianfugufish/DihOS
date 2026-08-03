#include "system/task_accounting.h"

static task_accounting_snapshot g_task_accounting;

void task_accounting_reset(void)
{
    g_task_accounting.frame_count = 0;
    for (uint32_t i = 0; i < TASK_ACCOUNT_BUCKET_COUNT; ++i)
        g_task_accounting.bucket_ticks[i] = 0;
}

void task_accounting_frame_begin(void)
{
    g_task_accounting.frame_count++;
}

void task_accounting_add(uint32_t bucket, uint64_t ticks)
{
    if (bucket >= TASK_ACCOUNT_BUCKET_COUNT || ticks == 0)
        return;
    g_task_accounting.bucket_ticks[bucket] += ticks;
}

void task_accounting_get(task_accounting_snapshot *out_snapshot)
{
    if (!out_snapshot)
        return;
    *out_snapshot = g_task_accounting;
}
