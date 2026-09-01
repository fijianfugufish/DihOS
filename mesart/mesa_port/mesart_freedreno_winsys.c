#include "mesart_freedreno_winsys.h"
#include "mesart_freedreno_pm4.h"
#include "mesart_protocol.h"

static int mesart_fd_query_device(uint64_t *out_abi, uint64_t *out_model)
{
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    register uint64_t x0 __asm__("x0") = MESART_OPERATION_QUERY_DEVICE;
    register uint64_t x1 __asm__("x1") = 0u;
    register uint64_t x8 __asm__("x8") = MESART_EL0_SYSCALL;

    __asm__ __volatile__("svc #0" : "+r"(x0), "+r"(x1)
                         : "r"(x8) : "memory");
    if (x0 != MESART_ABI_VERSION || x1 != MESART_DEVICE_ADRENO_X1_85)
        return -1;
    *out_abi = x0;
    *out_model = x1;
    return 0;
#else
    (void)out_abi;
    (void)out_model;
    return -1;
#endif
}

static int mesart_fd_query_chip_id(uint64_t *out_chip_id)
{
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    register uint64_t x0 __asm__("x0") = MESART_OPERATION_QUERY_CHIP_ID;
    register uint64_t x1 __asm__("x1") = 0u;
    register uint64_t x8 __asm__("x8") = MESART_EL0_SYSCALL;

    __asm__ __volatile__("svc #0" : "+r"(x0), "+r"(x1)
                         : "r"(x8) : "memory");
    if (x0 != 0u || x1 != MESART_FREEDRENO_CHIP_ID_ADRENO_X1_85)
        return -1;
    *out_chip_id = x1;
    return 0;
#else
    (void)out_chip_id;
    return -1;
#endif
}

int mesart_fd_device_init(mesart_fd_device *device)
{
    uint64_t abi = 0u;
    uint64_t model = 0u;
    uint64_t chip_id = 0u;

    if (!device || mesart_fd_query_device(&abi, &model) != 0 ||
        mesart_fd_query_chip_id(&chip_id) != 0 ||
        mesart_runtime_init(&device->runtime) != 0 ||
        mesart_runtime_gpu_arena_init(&device->gpu_arena) != 0)
        return -1;
    *device = (mesart_fd_device){
        .runtime = device->runtime,
        .gpu_arena = device->gpu_arena,
        .gpu_model = (uint32_t)model,
        .abi_version = (uint32_t)abi,
        .chip_id = chip_id,
        .ready = 1u,
    };
    return 0;
}

int mesart_fd_bo_alloc(mesart_fd_device *device, uint64_t bytes,
                       uint64_t alignment, uint32_t flags,
                       mesart_fd_bo *out_bo)
{
    void *cpu;
    uint64_t gpu_va = 0u;

    if (!out_bo)
        return -1;
    *out_bo = (mesart_fd_bo){0};
    if (!device || !device->ready || !bytes ||
        (flags & ~(MESART_FD_BO_COMMAND | MESART_FD_BO_RESOURCE)))
        return -2;
    if (!alignment)
        alignment = 4096u;
    cpu = mesart_runtime_gpu_alloc(&device->gpu_arena, bytes, alignment,
                                   &gpu_va);
    if (!cpu || !gpu_va)
        return -3;
    *out_bo = (mesart_fd_bo){cpu, gpu_va, bytes, flags};
    return 0;
}

int mesart_fd_submit_begin(mesart_fd_device *device,
                           mesart_fd_submit *out_submit)
{
    if (!out_submit)
        return -1;
    *out_submit = (mesart_fd_submit){0};
    if (!device || !device->ready ||
        mesart_runtime_command_init(&out_submit->command) != 0)
        return -2;
    return 0;
}

int mesart_fd_submit_flush(const mesart_fd_submit *submit, uint64_t dwords,
                           uint64_t *out_fence)
{
    if (!submit)
        return -1;
    return mesart_runtime_command_submit(&submit->command, dwords, out_fence);
}

int mesart_fd_winsys_selftest(void)
{
    mesart_fd_device device = {0};
    mesart_fd_bo resource = {0};
    uint32_t *word;

    if (mesart_fd_device_init(&device) != 0 ||
        device.gpu_model != MESART_DEVICE_ADRENO_X1_85 ||
        device.chip_id != MESART_FREEDRENO_CHIP_ID_ADRENO_X1_85 ||
        mesart_fd_bo_alloc(&device, 4096u, 4096u, MESART_FD_BO_RESOURCE,
                           &resource) != 0 ||
        !resource.cpu || (resource.gpu_va & 4095u) ||
        resource.bytes != 4096u)
        return -1;
    word = (uint32_t *)resource.cpu;
    *word = 0x57494e53u; /* "WINS" */
    return *word == 0x57494e53u ? 0 : -2;
}

int mesart_fd_submit_resource_write_selftest(void)
{
    mesart_fd_device device = {0};
    mesart_fd_bo resource = {0};
    mesart_fd_submit submit = {0};
    uint32_t *words;
    uint64_t fence = 0u;

    if (mesart_fd_device_init(&device) != 0 ||
        mesart_fd_bo_alloc(&device, 4096u, 4096u, MESART_FD_BO_RESOURCE,
                           &resource) != 0 ||
        mesart_fd_submit_begin(&device, &submit) != 0 ||
        !resource.cpu || !resource.gpu_va || !submit.command.cpu ||
        submit.command.capacity_dwords < 5u)
        return -1;

    words = submit.command.cpu;
    /* Generated Mesa framing, a broker-owned resource BO, and the narrow
     * primary-submit object meet here.  EL0 has no MMIO or raw ring access:
     * command capture, address validation, queueing, and the fence all stay
     * in the kernel. */
    words[0] = mesart_fd_a7xx_pkt7(0x3du, 3u); /* CP_MEM_WRITE */
    words[1] = (uint32_t)resource.gpu_va;
    words[2] = (uint32_t)(resource.gpu_va >> 32);
    words[3] = MESART_RESOURCE_WRITE_TEST_MAGIC;
    words[4] = mesart_fd_a7xx_pkt7(0x12u, 0u); /* CP_WAIT_MEM_WRITES */
    if (!words[0] || !words[4])
        return -2;
    if (mesart_fd_submit_flush(&submit, 5u, &fence) != 0 || !fence)
        return -3;
    return 0;
}
