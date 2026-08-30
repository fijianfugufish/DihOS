#pragma once

#include <stdint.h>

/* A driver describes its transition; the generic core validates it before a
 * platform implementation is ever allowed to execute it. */
typedef enum gpu_bringup_target
{
    GPU_BRINGUP_TARGET_GPUCC = 0,
    GPU_BRINGUP_TARGET_RSCC,
    GPU_BRINGUP_TARGET_GMU,
    GPU_BRINGUP_TARGET_COUNT,
} gpu_bringup_target;

typedef enum gpu_bringup_action
{
    /* A platform callback must complete this before any MMIO action. */
    GPU_BRINGUP_PLATFORM_VOTE = 0,
    GPU_BRINGUP_RMW_SET,
    GPU_BRINGUP_RMW_CLEAR,
    GPU_BRINGUP_POLL_SET,
    GPU_BRINGUP_SMMU_ATTACH,
    GPU_BRINGUP_FIRMWARE_UPLOAD,
    GPU_BRINGUP_START_GMU,
} gpu_bringup_action;

typedef struct gpu_bringup_op
{
    gpu_bringup_action action;
    gpu_bringup_target target;
    uint32_t offset;
    uint32_t mask;
} gpu_bringup_op;

typedef struct gpu_bringup_plan
{
    const char *driver_name;
    const gpu_bringup_op *ops;
    uint32_t op_count;
} gpu_bringup_plan;

typedef struct gpu_bringup_target_window
{
    uint64_t size_bytes;
    volatile uint8_t *cpu_base;
    uint32_t cpu_mapped;
} gpu_bringup_target_window;

/*
 * The core owns this executor; a platform owns the callbacks.  In
 * particular, a GPU driver must not know whether an SoC uses RPMh, SCMI,
 * ACPI or something else to make its power/clock vote.  Leaving a required
 * callback null keeps a plan non-executable.
 */
typedef struct gpu_bringup_backend
{
    void *context;
    int (*platform_vote)(void *context);
    int (*smmu_attach)(void *context);
    int (*firmware_upload)(void *context);
    int (*start_gmu)(void *context);
    uint32_t poll_limit;
} gpu_bringup_backend;

/* Verifies ordering and register bounds only. It does not touch hardware. */
int gpu_bringup_validate(const gpu_bringup_plan *plan,
                         const gpu_bringup_target_window windows[
                             GPU_BRINGUP_TARGET_COUNT]);

/* Executes a pre-validated plan.  MMIO is unreachable unless the platform
 * vote succeeds, and all non-MMIO transitions remain explicit callbacks. */
int gpu_bringup_execute(const gpu_bringup_plan *plan,
                        const gpu_bringup_target_window windows[
                            GPU_BRINGUP_TARGET_COUNT],
                        const gpu_bringup_backend *backend);
