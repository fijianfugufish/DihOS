#include "mesart/mesart_renderer.h"

#include "gpu/gpu_core.h"
#include "asm/asm.h"
#include "memory/pmem.h"
#include "mesart/mesart_packets.h"
#include "mesart/mesart_roots.h"
#include "mesart_protocol.h"

static void copy_sha256(uint8_t destination[MESART_SHA256_BYTES],
                        const uint8_t source[MESART_SHA256_BYTES])
{
    for (uint32_t i = 0u; i < MESART_SHA256_BYTES; ++i)
        destination[i] = source[i];
}

static uint32_t mesart_buffer_handle(uint32_t slot, uint16_t generation)
{
    return ((uint32_t)generation << 16) | (slot + 1u);
}

static uint32_t mesart_command_handle(uint16_t generation)
{
    return 0x80000001u | ((uint32_t)generation << 16);
}

static mesart_renderer_buffer *mesart_renderer_find_buffer(
    mesart_renderer_service *service, uint32_t handle)
{
    uint32_t encoded_slot;
    uint32_t slot;
    mesart_renderer_buffer *buffer;

    if (!service || !handle)
        return 0;
    encoded_slot = handle & 0xffffu;
    if (!encoded_slot)
        return 0;
    slot = encoded_slot - 1u;
    if (slot >= MESART_RENDERER_MAX_BUFFERS)
        return 0;
    buffer = &service->buffers[slot];
    return buffer->active && buffer->handle == handle ? buffer : 0;
}

/* This helper is admission-only: the VM is inert while its page tables are
 * changed.  A later allocate syscall must first grow the VM API with safe
 * active-root TLB maintenance; do not bypass that boundary here. */
static int mesart_renderer_create_buffer(mesart_renderer_service *service,
                                         uint64_t bytes,
                                         uint32_t *out_handle,
                                         uint64_t *out_user_va)
{
    mesart_renderer_buffer *buffer;
    uint64_t user_va;
    uint32_t slot;

    if (!service || !out_handle || !out_user_va || !bytes ||
        bytes > MESART_RENDERER_BUFFER_SLOT_BYTES ||
        (bytes & (AARCH64_USER_VM_PAGE_SIZE - 1u)))
        return -1;
    for (slot = 0u; slot < MESART_RENDERER_MAX_BUFFERS; ++slot)
        if (!service->buffers[slot].active)
            break;
    if (slot == MESART_RENDERER_MAX_BUFFERS)
        return -2;
    user_va = MESART_RENDERER_BUFFER_ARENA_BASE +
              (uint64_t)slot * MESART_RENDERER_BUFFER_SLOT_BYTES;
    if (user_va < MESART_RENDERER_BUFFER_ARENA_BASE ||
        gpu_buffer_alloc(&service->buffers[slot].storage, bytes,
                         GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0)
        return -3;
    buffer = &service->buffers[slot];
    if (aarch64_user_vm_map(&service->vm, user_va, buffer->storage.phys,
                            bytes, AARCH64_USER_VM_READ |
                                   AARCH64_USER_VM_WRITE) != 0)
    {
        gpu_buffer_release(&buffer->storage);
        return -4;
    }
    if (gpu_core_map_mesart_buffer(&buffer->storage,
                                   MESART_RENDERER_GPU_BUFFER_ARENA_BASE +
                                   (uint64_t)slot *
                                       MESART_RENDERER_BUFFER_SLOT_BYTES) != 0)
    {
        gpu_buffer_release(&buffer->storage);
        return -5;
    }
    buffer->generation = (uint16_t)(buffer->generation + 1u);
    if (!buffer->generation)
        buffer->generation = 1u;
    buffer->handle = mesart_buffer_handle(slot, buffer->generation);
    buffer->user_va = user_va;
    buffer->gpu_va = buffer->storage.iova;
    buffer->active = 1u;
    service->buffer_bytes += bytes;
    *out_handle = buffer->handle;
    *out_user_va = user_va;
    return 0;
}

static int mesart_renderer_create_command_buffer(
    mesart_renderer_service *service)
{
    mesart_renderer_command_buffer *command;

    if (!service || MESART_RENDERER_COMMAND_BUFFER_BYTES == 0u ||
        (MESART_RENDERER_COMMAND_BUFFER_BYTES & 3u) != 0u ||
        (MESART_RENDERER_COMMAND_BUFFER_BYTES &
         (AARCH64_USER_VM_PAGE_SIZE - 1u)) != 0u)
        return -1;
    command = &service->command_buffer;
    if (gpu_buffer_alloc(&command->source,
                         MESART_RENDERER_COMMAND_BUFFER_BYTES,
                         GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0)
        return -2;
    if (aarch64_user_vm_map(&service->vm,
                            MESART_RENDERER_COMMAND_BUFFER_VA,
                            command->source.phys,
                            MESART_RENDERER_COMMAND_BUFFER_BYTES,
                            AARCH64_USER_VM_READ | AARCH64_USER_VM_WRITE) != 0)
    {
        gpu_buffer_release(&command->source);
        return -3;
    }
    if (gpu_ring_init(&command->snapshot,
                      MESART_RENDERER_COMMAND_BUFFER_BYTES) != 0)
    {
        gpu_buffer_release(&command->source);
        return -4;
    }
    if (gpu_core_map_mesart_buffer(&command->source,
                                   MESART_RENDERER_COMMAND_BUFFER_GPU_VA) != 0)
    {
        gpu_ring_release(&command->snapshot);
        gpu_buffer_release(&command->source);
        return -5;
    }
    command->generation = 1u;
    command->handle = mesart_command_handle(command->generation);
    command->user_va = MESART_RENDERER_COMMAND_BUFFER_VA;
    command->gpu_va = command->source.iova;
    command->active = 1u;
    service->buffer_bytes += MESART_RENDERER_COMMAND_BUFFER_BYTES;
    return 0;
}

/* Mesa's initial runtime gets one bounded heap that is mapped during service
 * admission.  That preserves the EL0 VM invariant: no user request can add
 * a mapping while this address space is active. */
static int mesart_renderer_create_runtime_heap(mesart_renderer_service *service)
{
    if (!service || MESART_RENDERER_RUNTIME_HEAP_BYTES == 0u ||
        (MESART_RENDERER_RUNTIME_HEAP_BYTES &
         (AARCH64_USER_VM_PAGE_SIZE - 1u)) != 0u ||
        (MESART_RENDERER_RUNTIME_HEAP_VA &
         (AARCH64_USER_VM_PAGE_SIZE - 1u)) != 0u)
        return -1;
    if (gpu_buffer_alloc(&service->runtime_heap,
                         MESART_RENDERER_RUNTIME_HEAP_BYTES,
                         GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0)
        return -2;
    if (aarch64_user_vm_map(&service->vm, MESART_RENDERER_RUNTIME_HEAP_VA,
                            service->runtime_heap.phys,
                            MESART_RENDERER_RUNTIME_HEAP_BYTES,
                            AARCH64_USER_VM_READ |
                            AARCH64_USER_VM_WRITE) != 0)
    {
        gpu_buffer_release(&service->runtime_heap);
        return -3;
    }
    service->buffer_bytes += MESART_RENDERER_RUNTIME_HEAP_BYTES;
    return 0;
}

static int mesart_renderer_capture_command_buffer(
    mesart_renderer_service *service, uint32_t handle, uint32_t dwords)
{
    mesart_renderer_command_buffer *command;
    mesart_packet_validation validation;
    mesart_packet_policy policy;

    if (!service)
        return -1;
    command = &service->command_buffer;
    if (!command->active || command->handle != handle ||
        !command->source.cpu || !command->snapshot.buffer.cpu ||
        command->snapshot.sealed || !dwords ||
        dwords > command->snapshot.capacity_dwords)
        return -2;
    policy = (mesart_packet_policy){
        service->buffers[1].gpu_va,
        service->buffers[1].storage.size_bytes,
        1u,
        1u,
    };
    if (mesart_validate_a7xx_cp_stream((const uint32_t *)command->source.cpu,
                                       dwords, &policy, &validation) != 0)
        return -3;
    if (gpu_ring_emit_many(&command->snapshot,
                           (const uint32_t *)command->source.cpu,
                           dwords) != 0 ||
        gpu_ring_seal(&command->snapshot) != 0)
        return -4;
    command->packet_count = validation.packet_count;
    return 0;
}

static int mesart_renderer_queue_command_buffer(
    mesart_renderer_service *service, uint32_t handle, uint64_t *out_fence)
{
    mesart_renderer_command_buffer *command;
    gpu_scheduler_submission submission;

    if (!service || !out_fence)
        return -1;
    command = &service->command_buffer;
    if (!command->active)
        return -20;
    if (command->handle != handle)
        return -21;
    if (!command->snapshot.sealed)
        return -22;
    if (!command->snapshot.write_dwords)
        return -23;
    if (command->queued_fence)
        return -24;
    submission = (gpu_scheduler_submission){
        .ring = &command->snapshot,
        .bytes_in_flight =
            (uint64_t)command->snapshot.write_dwords * sizeof(uint32_t),
        .user_tag = command->packet_count,
        .writable_gpu_va = service->buffers[1].gpu_va,
        .writable_gpu_bytes = service->buffers[1].storage.size_bytes,
        .policy_flags = GPU_SCHEDULER_POLICY_A7XX_RESOURCE_WRITE |
                        GPU_SCHEDULER_POLICY_A7XX_SYNC,
    };
    if (gpu_scheduler_submit(gpu_core_scheduler(), service->scheduler_client,
                             &submission, &command->queued_fence) != 0)
        return -3;
    *out_fence = command->queued_fence;
    return 0;
}

void mesart_renderer_release(mesart_renderer_service *service)
{
    gpu_scheduler *scheduler;

    if (!service)
        return;
    if (service->scheduler_registered)
    {
        scheduler = gpu_core_scheduler();
        if (gpu_scheduler_unregister_client(scheduler,
                                            service->scheduler_client) != 0)
        {
            (void)gpu_scheduler_fail_client(scheduler, service->scheduler_client);
            (void)gpu_scheduler_unregister_client(scheduler,
                                                   service->scheduler_client);
        }
    }
    aarch64_user_vm_release(&service->vm);
    mesart_elf_release_image(&service->image);
    if (service->stack_memory && service->stack_pages)
        pmem_free_pages(service->stack_memory, service->stack_pages);
    for (uint32_t i = 0u; i < MESART_RENDERER_MAX_BUFFERS; ++i)
        gpu_buffer_release(&service->buffers[i].storage);
    gpu_ring_release(&service->command_buffer.snapshot);
    gpu_buffer_release(&service->command_buffer.source);
    gpu_buffer_release(&service->runtime_heap);
    mesart_bundle_release(&service->bundle);
    *service = (mesart_renderer_service){0};
}

int mesart_renderer_syscall(aa64_el0_frame *frame, void *context)
{
    mesart_renderer_service *service = (mesart_renderer_service *)context;

    if (!frame || !service || !service->image.memory ||
        frame->x[8] != MESART_EL0_SYSCALL)
        return 0;
    service->last_syscall_operation = (uint32_t)frame->x[0];
    service->last_syscall_result = 0;
    switch ((uint32_t)frame->x[0])
    {
    case MESART_OPERATION_QUERY_DEVICE:
        frame->x[0] = MESART_ABI_VERSION;
        frame->x[1] = MESART_DEVICE_ADRENO_X1_85;
        frame->x[2] = MESART_FEATURE_QUERY_DEVICE |
                      MESART_FEATURE_BOOTSTRAP_BUFFER |
                      MESART_FEATURE_COMMAND_SNAPSHOT |
                      MESART_FEATURE_COMMAND_VALIDATION |
                      MESART_FEATURE_SCHEDULER_QUEUE |
                      MESART_FEATURE_RUNTIME_HEAP |
                      MESART_FEATURE_GPU_BUFFER_VA |
                      MESART_FEATURE_GPU_RESOURCE_ARENA |
                      MESART_FEATURE_FREEDRENO_CHIP_ID |
                      MESART_FEATURE_COMMAND_SYNC;
        frame->x[3] = service->bootstrap_buffer_handle;
        frame->x[4] = service->buffers[0].user_va;
        frame->x[5] = service->buffers[0].storage.size_bytes;
        return 1;
    case MESART_OPERATION_QUERY_BUFFER:
    {
        mesart_renderer_buffer *buffer = mesart_renderer_find_buffer(
            service, (uint32_t)frame->x[1]);

        if (!buffer)
        {
            frame->x[0] = (uint64_t)-2; /* Invalid/stale opaque handle. */
            service->last_syscall_result = -2;
            return 1;
        }
        frame->x[0] = 0u;
        frame->x[1] = buffer->user_va;
        frame->x[2] = buffer->storage.size_bytes;
        frame->x[3] = buffer->gpu_va;
        return 1;
    }
    case MESART_OPERATION_QUERY_COMMAND_BUFFER:
        if (!service->command_buffer.active)
        {
            frame->x[0] = (uint64_t)-2;
            service->last_syscall_result = -2;
            return 1;
        }
        frame->x[0] = 0u;
        frame->x[1] = service->command_buffer.handle;
        frame->x[2] = service->command_buffer.user_va;
        frame->x[3] = service->command_buffer.snapshot.capacity_dwords;
        frame->x[4] = service->command_buffer.gpu_va;
        return 1;
    case MESART_OPERATION_CAPTURE_COMMAND_BUFFER:
        service->last_syscall_result = mesart_renderer_capture_command_buffer(
            service, (uint32_t)frame->x[1], (uint32_t)frame->x[2]);
        frame->x[0] = (uint64_t)service->last_syscall_result;
        return 1;
    case MESART_OPERATION_QUEUE_CAPTURED_COMMAND:
    {
        uint64_t fence = 0u;
        int rc = mesart_renderer_queue_command_buffer(
            service, (uint32_t)frame->x[1], &fence);

        frame->x[0] = (uint64_t)rc;
        frame->x[1] = fence;
        service->last_syscall_result = rc;
        return 1;
    }
    case MESART_OPERATION_QUERY_RUNTIME:
        if (!service->runtime_heap.cpu ||
            service->runtime_heap.size_bytes !=
                MESART_RENDERER_RUNTIME_HEAP_BYTES)
        {
            frame->x[0] = (uint64_t)-2;
            service->last_syscall_result = -2;
            return 1;
        }
        frame->x[0] = 0u;
        frame->x[1] = MESART_RENDERER_RUNTIME_HEAP_VA;
        frame->x[2] = service->runtime_heap.size_bytes;
        frame->x[3] = MESART_RUNTIME_ABI_VERSION;
        return 1;
    case MESART_OPERATION_QUERY_GPU_ARENA:
    {
        mesart_renderer_buffer *arena = mesart_renderer_find_buffer(
            service, service->resource_arena_handle);

        if (!arena || !arena->gpu_va ||
            arena->storage.size_bytes != MESART_RENDERER_BUFFER_SLOT_BYTES)
        {
            frame->x[0] = (uint64_t)-2;
            service->last_syscall_result = -2;
            return 1;
        }
        frame->x[0] = 0u;
        frame->x[1] = arena->handle;
        frame->x[2] = arena->user_va;
        frame->x[3] = arena->storage.size_bytes;
        frame->x[4] = arena->gpu_va;
        return 1;
    }
    case MESART_OPERATION_QUERY_CHIP_ID:
        frame->x[0] = 0u;
        frame->x[1] = MESART_FREEDRENO_CHIP_ID_ADRENO_X1_85;
        return 1;
    default:
        frame->x[0] = (uint64_t)-38; /* ENOSYS within Mesart's own ABI. */
        service->last_syscall_result = -38;
        return 1;
    }
}

int mesart_renderer_test_complete_queued(mesart_renderer_service *service)
{
    mesart_renderer_command_buffer *command;
    mesart_renderer_buffer *arena;
    uint64_t completed_fence = 0u;

    if (!service)
        return -1;
    command = &service->command_buffer;
    if (!command->queued_fence)
        return -2;
    /* The first runtime backend has a deliberately tiny hardware contract:
     * it revalidates and executes only the sealed NOP/order/resource-write
     * snapshot that EL0 just queued.  A successful scheduler completion
     * therefore means the live CP read pointer consumed that batch, not a
     * synthetic test signal. */
    service->last_backend_result = gpu_core_execute_next_scheduled(
        &completed_fence);
    if (service->last_backend_result != 0 ||
        completed_fence != command->queued_fence)
        return -3;
    arena = mesart_renderer_find_buffer(service, service->resource_arena_handle);
    if (!arena || !arena->storage.cpu || arena->storage.size_bytes < 4u)
        return -4;
    asm_dma_invalidate_range(arena->storage.cpu, sizeof(uint32_t));
    if (*(uint32_t *)arena->storage.cpu != MESART_RESOURCE_WRITE_TEST_MAGIC)
        return -5;
    command->completed_fence = completed_fence;
    command->queued_fence = 0u;
    return 0;
}

int mesart_renderer_admit(dihos_process_table *processes,
                          const char *bundle_root,
                          mesart_renderer_service *out_service)
{
#if !defined(__aarch64__) && !defined(__arm64__) && !defined(_M_ARM64)
    (void)processes;
    (void)bundle_root;
    (void)out_service;
    return -100;
#else
    mesart_renderer_service service = {0};
    const mesart_trust_root *root;
    const mesart_manifest_file *renderer;
    dihos_process_desc process_desc = {0};
    dihos_process_info process_info;
    gpu_scheduler_client_desc scheduler_desc;
    uint64_t stack_physical;
    uint64_t stack_bytes = MESART_RENDERER_STACK_PAGES *
                           AARCH64_USER_VM_PAGE_SIZE;
    int rc;

    if (!processes || !bundle_root || !out_service)
        return -1;
    *out_service = (mesart_renderer_service){0};
    root = mesart_kernel_trust_root();
    if (!root)
        return -2; /* No configured debug/release root for this kernel. */
    if ((MESART_RENDERER_STACK_TOP &
         (AARCH64_USER_VM_PAGE_SIZE - 1u)) ||
        MESART_RENDERER_STACK_TOP < stack_bytes)
        return -3;
    rc = mesart_bundle_open(bundle_root, root, &service.bundle);
    if (rc != 0)
        return -10 + rc;
    renderer = mesart_bundle_renderer_file(&service.bundle);
    if (!renderer)
    {
        rc = -20;
        goto failed;
    }
    rc = aarch64_user_vm_create(&service.vm);
    if (rc != 0)
    {
        rc = -30 + rc;
        goto failed;
    }
    rc = mesart_elf_load_verified(bundle_root, renderer, &service.vm,
                                  MESART_ELF_DEFAULT_BASE_VA,
                                  &service.image);
    if (rc != 0)
    {
        rc = -40 + rc;
        goto failed;
    }
    service.stack_memory = pmem_alloc_pages(MESART_RENDERER_STACK_PAGES);
    service.stack_pages = MESART_RENDERER_STACK_PAGES;
    if (!service.stack_memory ||
        aarch64_user_vm_translate_current(
            (uint64_t)(uintptr_t)service.stack_memory, &stack_physical) != 0 ||
        aarch64_user_vm_map(&service.vm,
                            MESART_RENDERER_STACK_TOP - stack_bytes,
                            stack_physical, stack_bytes,
                            AARCH64_USER_VM_READ | AARCH64_USER_VM_WRITE) != 0)
    {
        rc = -50;
        goto failed;
    }
    service.stack_top_va = MESART_RENDERER_STACK_TOP - 16u;
    if (mesart_renderer_create_buffer(&service,
                                      MESART_RENDERER_BOOTSTRAP_BUFFER_BYTES,
                                      &service.bootstrap_buffer_handle,
                                      &service.buffers[0].user_va) != 0)
    {
        rc = -55;
        goto failed;
    }
    if (mesart_renderer_create_buffer(&service,
                                      MESART_RENDERER_BUFFER_SLOT_BYTES,
                                      &service.resource_arena_handle,
                                      &service.buffers[1].user_va) != 0)
    {
        rc = -56;
        goto failed;
    }
    if (mesart_renderer_create_command_buffer(&service) != 0)
    {
        rc = -57;
        goto failed;
    }
    if (mesart_renderer_create_runtime_heap(&service) != 0)
    {
        rc = -58;
        goto failed;
    }
    process_desc.kind = DIHOS_PROCESS_KIND_RENDERER_SERVICE;
    process_desc.memory_limit_bytes = service.image.virtual_bytes + stack_bytes +
                                      service.buffer_bytes;
    process_desc.capabilities = DIHOS_PROCESS_CAP_BUNDLE_READ |
                                DIHOS_PROCESS_CAP_IPC |
                                DIHOS_PROCESS_CAP_CLOCK |
                                DIHOS_PROCESS_CAP_GFX_RENDERER |
                                DIHOS_PROCESS_CAP_COMPOSITOR_SURFACE;
    copy_sha256(process_desc.image_sha256, renderer->sha256);
    rc = dihos_process_create(processes, &process_desc, &service.process);
    if (rc != 0 || dihos_process_set_ready(processes, service.process) != 0)
    {
        if (rc == 0)
        {
            (void)dihos_process_fault(processes, service.process);
            (void)dihos_process_reap(processes, service.process);
        }
        rc = -60;
        goto failed;
    }
    if (dihos_process_query(processes, service.process, &process_info) != 0)
    {
        (void)dihos_process_fault(processes, service.process);
        (void)dihos_process_reap(processes, service.process);
        rc = -61;
        goto failed;
    }
    scheduler_desc = (gpu_scheduler_client_desc){
        GPU_SCHEDULER_CLIENT_MESA_SERVICE,
        process_info.address_space_id,
        MESART_RENDERER_MAX_BYTES_IN_FLIGHT,
        MESART_RENDERER_MAX_JOBS_IN_FLIGHT,
    };
    if (gpu_scheduler_register_client(gpu_core_scheduler(), &scheduler_desc,
                                      &service.scheduler_client) != 0)
    {
        (void)dihos_process_fault(processes, service.process);
        (void)dihos_process_reap(processes, service.process);
        rc = -62;
        goto failed;
    }
    service.scheduler_registered = 1u;
    *out_service = service;
    return 0;
failed:
    mesart_renderer_release(&service);
    return rc;
#endif
}
