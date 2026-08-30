#include "gpu/gpu_bringup.h"
#include "asm/asm.h"

static int is_mmio_action(gpu_bringup_action action)
{
    return action == GPU_BRINGUP_RMW_SET ||
           action == GPU_BRINGUP_RMW_CLEAR ||
           action == GPU_BRINGUP_POLL_SET;
}

static int mmio_read32(const gpu_bringup_target_window *window,
                       uint32_t offset, uint32_t *value)
{
    return asm_aa64_try_read32((uint64_t)(uintptr_t)window->cpu_base + offset,
                               value);
}

static int mmio_write32(const gpu_bringup_target_window *window,
                        uint32_t offset, uint32_t value)
{
    return asm_aa64_try_write32((uint64_t)(uintptr_t)window->cpu_base + offset,
                                value);
}

int gpu_bringup_validate(const gpu_bringup_plan *plan,
                         const gpu_bringup_target_window windows[
                             GPU_BRINGUP_TARGET_COUNT])
{
    uint32_t saw_platform_vote = 0u;

    if (!plan || !plan->driver_name || !plan->ops || !plan->op_count ||
        !windows)
        return -1;

    for (uint32_t i = 0u; i < plan->op_count; ++i)
    {
        const gpu_bringup_op *op = &plan->ops[i];
        if (op->action > GPU_BRINGUP_START_GMU ||
            op->target >= GPU_BRINGUP_TARGET_COUNT)
            return -2;
        if (op->action == GPU_BRINGUP_PLATFORM_VOTE)
        {
            if (i != 0u || op->mask)
                return -3;
            saw_platform_vote = 1u;
            continue;
        }
        if (!saw_platform_vote)
            return -4;
        if (is_mmio_action(op->action))
        {
            if (!op->mask || !windows[op->target].cpu_mapped ||
                op->offset > windows[op->target].size_bytes ||
                windows[op->target].size_bytes - op->offset < 4u)
                return -5;
        }
    }
    return 0;
}

int gpu_bringup_execute(const gpu_bringup_plan *plan,
                        const gpu_bringup_target_window windows[
                            GPU_BRINGUP_TARGET_COUNT],
                        const gpu_bringup_backend *backend)
{
    uint32_t poll_limit;

    if (!backend || gpu_bringup_validate(plan, windows) != 0)
        return -1;
    for (uint32_t target = 0u; target < GPU_BRINGUP_TARGET_COUNT; ++target)
        if (windows[target].cpu_mapped && !windows[target].cpu_base)
            return -3;

    poll_limit = backend->poll_limit ? backend->poll_limit : 1000000u;
    for (uint32_t i = 0u; i < plan->op_count; ++i)
    {
        const gpu_bringup_op *op = &plan->ops[i];
        const gpu_bringup_target_window *window = &windows[op->target];
        uint32_t value;

        switch (op->action)
        {
        case GPU_BRINGUP_PLATFORM_VOTE:
            if (!backend->platform_vote)
                return -10;
            if (backend->platform_vote(backend->context) != 0)
                return -11;
            break;
        case GPU_BRINGUP_RMW_SET:
            if (mmio_read32(window, op->offset, &value) != 0)
                return -20;
            if (mmio_write32(window, op->offset, value | op->mask) != 0)
                return -21;
            break;
        case GPU_BRINGUP_RMW_CLEAR:
            if (mmio_read32(window, op->offset, &value) != 0)
                return -20;
            if (mmio_write32(window, op->offset, value & ~op->mask) != 0)
                return -21;
            break;
        case GPU_BRINGUP_POLL_SET:
            do
            {
                if (mmio_read32(window, op->offset, &value) != 0)
                    return -22;
                if ((value & op->mask) == op->mask)
                    break;
            } while (--poll_limit);
            if (!poll_limit)
                return -11;
            break;
        case GPU_BRINGUP_SMMU_ATTACH:
            if (!backend->smmu_attach)
                return -12;
            if (backend->smmu_attach(backend->context) != 0)
                return -13;
            break;
        case GPU_BRINGUP_FIRMWARE_UPLOAD:
            if (!backend->firmware_upload)
                return -14;
            if (backend->firmware_upload(backend->context) != 0)
                return -15;
            break;
        case GPU_BRINGUP_START_GMU:
            if (!backend->start_gmu)
                return -16;
            if (backend->start_gmu(backend->context) != 0)
                return -17;
            break;
        default:
            return -15;
        }
    }
    return 0;
}
