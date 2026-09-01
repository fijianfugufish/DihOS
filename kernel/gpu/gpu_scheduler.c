#include "gpu/gpu_scheduler.h"

static int client_index(const gpu_scheduler *scheduler,
                        gpu_scheduler_client client,
                        uint32_t *out_index)
{
    const gpu_scheduler_client_state *state;

    if (!scheduler || !out_index || client.slot >= GPU_SCHEDULER_MAX_CLIENTS)
        return -1;
    state = &scheduler->clients[client.slot];
    if (!state->active || state->generation != client.generation)
        return -2;
    *out_index = client.slot;
    return 0;
}

static int scheduler_job_index(const gpu_scheduler *scheduler, uint64_t fence,
                               uint32_t *out_index)
{
    if (!scheduler || !out_index || !fence)
        return -1;
    for (uint32_t i = 0u; i < scheduler->job_count; ++i)
        if (scheduler->jobs[i].fence == fence)
        {
            *out_index = i;
            return 0;
        }
    return -2;
}

static void scheduler_remove_job(gpu_scheduler *scheduler, uint32_t index)
{
    if (index >= scheduler->job_count)
        return;
    for (uint32_t i = index + 1u; i < scheduler->job_count; ++i)
        scheduler->jobs[i - 1u] = scheduler->jobs[i];
    --scheduler->job_count;
}

void gpu_scheduler_init(gpu_scheduler *scheduler)
{
    if (scheduler)
        *scheduler = (gpu_scheduler){.next_fence = 1u};
}

int gpu_scheduler_register_client(gpu_scheduler *scheduler,
                                  const gpu_scheduler_client_desc *desc,
                                  gpu_scheduler_client *out_client)
{
    gpu_scheduler_client_state *state;

    if (!scheduler || !desc || !out_client ||
        (desc->kind != GPU_SCHEDULER_CLIENT_KERNEL_COMPOSITOR &&
         desc->kind != GPU_SCHEDULER_CLIENT_MESA_SERVICE) ||
        !desc->max_bytes_in_flight || !desc->max_jobs_in_flight ||
        desc->max_jobs_in_flight > GPU_SCHEDULER_MAX_JOBS ||
        (desc->kind == GPU_SCHEDULER_CLIENT_MESA_SERVICE &&
         !desc->address_space_id))
        return -1;

    for (uint32_t i = 0u; i < GPU_SCHEDULER_MAX_CLIENTS; ++i)
    {
        state = &scheduler->clients[i];
        if (state->active)
            continue;
        state->generation = (uint16_t)(state->generation + 1u);
        if (!state->generation)
            state->generation = 1u;
        state->desc = *desc;
        state->bytes_in_flight = 0u;
        state->jobs_in_flight = 0u;
        state->active = 1u;
        *out_client = (gpu_scheduler_client){(uint16_t)i, state->generation};
        return 0;
    }
    return -2;
}

int gpu_scheduler_unregister_client(gpu_scheduler *scheduler,
                                    gpu_scheduler_client client)
{
    uint32_t index;

    if (client_index(scheduler, client, &index) != 0)
        return -1;
    if (scheduler->clients[index].jobs_in_flight)
        return -2;
    scheduler->clients[index].active = 0u;
    return 0;
}

int gpu_scheduler_submit(gpu_scheduler *scheduler,
                         gpu_scheduler_client client,
                         const gpu_scheduler_submission *submission,
                         uint64_t *out_fence)
{
    gpu_scheduler_client_state *state;
    uint32_t index;
    uint64_t fence;

    if (!scheduler || !submission || !out_fence ||
        !submission->ring || !submission->ring->sealed ||
        !submission->ring->write_dwords ||
        submission->ring->write_dwords > submission->ring->capacity_dwords ||
        !submission->bytes_in_flight ||
        !submission->policy_flags ||
        (submission->policy_flags &
         ~(GPU_SCHEDULER_POLICY_A7XX_NOP_ONLY |
           GPU_SCHEDULER_POLICY_A7XX_RESOURCE_WRITE |
           GPU_SCHEDULER_POLICY_A7XX_SYNC)) ||
        ((submission->policy_flags & GPU_SCHEDULER_POLICY_A7XX_RESOURCE_WRITE) &&
         (!submission->writable_gpu_va || !submission->writable_gpu_bytes ||
          submission->writable_gpu_va + submission->writable_gpu_bytes <
              submission->writable_gpu_va)) ||
        client_index(scheduler, client, &index) != 0)
        return -1;
    state = &scheduler->clients[index];
    if (scheduler->job_count >= GPU_SCHEDULER_MAX_JOBS ||
        state->jobs_in_flight >= state->desc.max_jobs_in_flight ||
        submission->bytes_in_flight > state->desc.max_bytes_in_flight -
                                      state->bytes_in_flight)
        return -2;
    fence = scheduler->next_fence++;
    if (!fence)
        fence = scheduler->next_fence++;
    scheduler->jobs[scheduler->job_count++] = (gpu_scheduler_job){
        client, submission->ring, fence, submission->bytes_in_flight,
        submission->user_tag, submission->writable_gpu_va,
        submission->writable_gpu_bytes, submission->policy_flags};
    state->bytes_in_flight += submission->bytes_in_flight;
    ++state->jobs_in_flight;
    *out_fence = fence;
    return 0;
}

int gpu_scheduler_peek_next(const gpu_scheduler *scheduler,
                            gpu_scheduler_job *out_job)
{
    uint32_t first_renderer = GPU_SCHEDULER_MAX_JOBS;

    if (!scheduler || !out_job || !scheduler->job_count)
        return -1;
    for (uint32_t i = 0u; i < scheduler->job_count; ++i)
    {
        const gpu_scheduler_job *job = &scheduler->jobs[i];
        const gpu_scheduler_client_state *state =
            &scheduler->clients[job->client.slot];
        if (state->desc.kind == GPU_SCHEDULER_CLIENT_KERNEL_COMPOSITOR)
        {
            *out_job = *job;
            return 0;
        }
        if (first_renderer == GPU_SCHEDULER_MAX_JOBS)
            first_renderer = i;
        if (job->client.slot >= scheduler->mesa_round_robin_cursor)
        {
            *out_job = *job;
            return 0;
        }
    }
    if (first_renderer != GPU_SCHEDULER_MAX_JOBS)
    {
        *out_job = scheduler->jobs[first_renderer];
        return 0;
    }
    return -2;
}

int gpu_scheduler_complete(gpu_scheduler *scheduler, uint64_t fence)
{
    gpu_scheduler_client_state *state;
    uint32_t job_index;
    uint32_t client_slot;

    if (scheduler_job_index(scheduler, fence, &job_index) != 0)
        return -1;
    client_slot = scheduler->jobs[job_index].client.slot;
    state = &scheduler->clients[client_slot];
    if (!state->active || !state->jobs_in_flight ||
        state->bytes_in_flight < scheduler->jobs[job_index].bytes_in_flight)
        return -2;
    state->bytes_in_flight -= scheduler->jobs[job_index].bytes_in_flight;
    --state->jobs_in_flight;
    if (state->desc.kind == GPU_SCHEDULER_CLIENT_MESA_SERVICE)
        scheduler->mesa_round_robin_cursor = (client_slot + 1u) %
                                             GPU_SCHEDULER_MAX_CLIENTS;
    scheduler_remove_job(scheduler, job_index);
    return 0;
}

uint32_t gpu_scheduler_fail_client(gpu_scheduler *scheduler,
                                   gpu_scheduler_client client)
{
    uint32_t index;
    uint32_t dropped = 0u;

    if (client_index(scheduler, client, &index) != 0)
        return 0u;
    for (uint32_t i = 0u; i < scheduler->job_count;)
    {
        if (scheduler->jobs[i].client.slot == index &&
            scheduler->jobs[i].client.generation == client.generation)
        {
            scheduler_remove_job(scheduler, i);
            ++dropped;
        }
        else
            ++i;
    }
    scheduler->clients[index].bytes_in_flight = 0u;
    scheduler->clients[index].jobs_in_flight = 0u;
    return dropped;
}
