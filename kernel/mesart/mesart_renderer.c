#include "mesart/mesart_renderer.h"

#include "memory/pmem.h"
#include "mesart/mesart_roots.h"
#include "mesart_protocol.h"

static void copy_sha256(uint8_t destination[MESART_SHA256_BYTES],
                        const uint8_t source[MESART_SHA256_BYTES])
{
    for (uint32_t i = 0u; i < MESART_SHA256_BYTES; ++i)
        destination[i] = source[i];
}

void mesart_renderer_release(mesart_renderer_service *service)
{
    if (!service)
        return;
    aarch64_user_vm_release(&service->vm);
    mesart_elf_release_image(&service->image);
    if (service->stack_memory && service->stack_pages)
        pmem_free_pages(service->stack_memory, service->stack_pages);
    mesart_bundle_release(&service->bundle);
    *service = (mesart_renderer_service){0};
}

int mesart_renderer_syscall(aa64_el0_frame *frame, void *context)
{
    mesart_renderer_service *service = (mesart_renderer_service *)context;

    if (!frame || !service || !service->image.memory ||
        frame->x[8] != MESART_EL0_SYSCALL)
        return 0;
    switch ((uint32_t)frame->x[0])
    {
    case MESART_OPERATION_QUERY_DEVICE:
        frame->x[0] = MESART_ABI_VERSION;
        frame->x[1] = MESART_DEVICE_ADRENO_X1_85;
        frame->x[2] = MESART_FEATURE_QUERY_DEVICE;
        return 1;
    default:
        frame->x[0] = (uint64_t)-38; /* ENOSYS within Mesart's own ABI. */
        return 1;
    }
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
    process_desc.kind = DIHOS_PROCESS_KIND_RENDERER_SERVICE;
    process_desc.memory_limit_bytes = service.image.virtual_bytes + stack_bytes;
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
    *out_service = service;
    return 0;
failed:
    mesart_renderer_release(&service);
    return rc;
#endif
}
