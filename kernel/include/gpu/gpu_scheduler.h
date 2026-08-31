#pragma once

#include <stdint.h>
#include "gpu/gpu_ring.h"

/*
 * The scheduler is deliberately above a GPU-family command encoder and below
 * the future Mesa renderer-service IPC layer.  It owns neither MMIO nor scanout;
 * a device backend takes the selected sealed batch and performs the hardware
 * submission.  This keeps the kernel compositor and unprivileged clients on
 * the same scheduling/fence path without ever sharing a command ring.
 */

#define GPU_SCHEDULER_MAX_CLIENTS  16u
#define GPU_SCHEDULER_MAX_JOBS     64u

typedef enum gpu_scheduler_client_kind
{
    /* Trusted build-time compositor effects: wallpaper, composition, fades. */
    GPU_SCHEDULER_CLIENT_KERNEL_COMPOSITOR = 1,
    /* The verified EL0 Mesa/Freedreno renderer service.  It is started and
     * controlled by the kernel, not exposed as a general app platform. */
    GPU_SCHEDULER_CLIENT_MESA_SERVICE = 2,
} gpu_scheduler_client_kind;

typedef struct gpu_scheduler_client
{
    uint16_t slot;
    uint16_t generation;
} gpu_scheduler_client;

#define GPU_SCHEDULER_INVALID_CLIENT ((gpu_scheduler_client){0xffffu, 0u})

typedef struct gpu_scheduler_client_desc
{
    gpu_scheduler_client_kind kind;
    /* A nonzero identity names the renderer service's GPU VM.  The compositor
     * uses identity 0 until it moves from the boot-domain to its own GPU VM. */
    uint64_t address_space_id;
    uint64_t max_bytes_in_flight;
    uint32_t max_jobs_in_flight;
} gpu_scheduler_client_desc;

typedef struct gpu_scheduler_submission
{
    /* Must remain allocated and sealed until the completion fence signals. */
    const gpu_command_ring *ring;
    uint64_t bytes_in_flight;
    uint64_t user_tag;
} gpu_scheduler_submission;

typedef struct gpu_scheduler_job
{
    gpu_scheduler_client client;
    const gpu_command_ring *ring;
    uint64_t fence;
    uint64_t bytes_in_flight;
    uint64_t user_tag;
} gpu_scheduler_job;

typedef struct gpu_scheduler_client_state
{
    gpu_scheduler_client_desc desc;
    uint64_t bytes_in_flight;
    uint32_t jobs_in_flight;
    uint16_t generation;
    uint8_t active;
} gpu_scheduler_client_state;

typedef struct gpu_scheduler
{
    gpu_scheduler_client_state clients[GPU_SCHEDULER_MAX_CLIENTS];
    gpu_scheduler_job jobs[GPU_SCHEDULER_MAX_JOBS];
    uint32_t job_count;
    uint32_t mesa_round_robin_cursor;
    uint64_t next_fence;
} gpu_scheduler;

void gpu_scheduler_init(gpu_scheduler *scheduler);

int gpu_scheduler_register_client(gpu_scheduler *scheduler,
                                  const gpu_scheduler_client_desc *desc,
                                  gpu_scheduler_client *out_client);
int gpu_scheduler_unregister_client(gpu_scheduler *scheduler,
                                    gpu_scheduler_client client);

/* Queues an already validated, sealed batch.  It does not issue any MMIO. */
int gpu_scheduler_submit(gpu_scheduler *scheduler,
                         gpu_scheduler_client client,
                         const gpu_scheduler_submission *submission,
                         uint64_t *out_fence);

/* Selects, but does not remove, the next job.  Kernel compositor work wins
 * over ordinary clients; renderer-service jobs are otherwise round-robin. */
int gpu_scheduler_peek_next(const gpu_scheduler *scheduler,
                            gpu_scheduler_job *out_job);

/* A backend calls complete only after the selected batch's fence is known to
 * be signalled, or fails_client after a reset/fault invalidates its work. */
int gpu_scheduler_complete(gpu_scheduler *scheduler, uint64_t fence);
uint32_t gpu_scheduler_fail_client(gpu_scheduler *scheduler,
                                   gpu_scheduler_client client);
