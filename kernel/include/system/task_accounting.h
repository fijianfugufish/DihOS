#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    TASK_ACCOUNT_INPUT_UI = 0,
    TASK_ACCOUNT_KERNEL_APPS = 1,
    TASK_ACCOUNT_TERMINAL = 2,
    TASK_ACCOUNT_SACX = 3,
    TASK_ACCOUNT_RENDER = 4,
    TASK_ACCOUNT_OTHER = 5,
    TASK_ACCOUNT_BUCKET_COUNT = 6
};

typedef struct task_accounting_snapshot
{
    uint64_t frame_count;
    uint64_t bucket_ticks[TASK_ACCOUNT_BUCKET_COUNT];
} task_accounting_snapshot;

void task_accounting_reset(void);
void task_accounting_frame_begin(void);
void task_accounting_add(uint32_t bucket, uint64_t ticks);
void task_accounting_get(task_accounting_snapshot *out_snapshot);

#ifdef __cplusplus
}
#endif
