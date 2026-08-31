#include "process/dihos_process.h"

static int process_slot_index(const dihos_process_table *table,
                              dihos_process_handle handle,
                              uint32_t *out_index)
{
    const dihos_process_slot *slot;

    if (!table || !out_index || handle.slot >= DIHOS_PROCESS_MAX)
        return -1;
    slot = &table->slots[handle.slot];
    if (!slot->active || slot->generation != handle.generation)
        return -2;
    *out_index = handle.slot;
    return 0;
}

static int process_desc_valid(const dihos_process_desc *desc)
{
    if (!desc ||
        (desc->kind != DIHOS_PROCESS_KIND_RENDERER_SERVICE &&
         desc->kind != DIHOS_PROCESS_KIND_NATIVE) ||
        desc->memory_limit_bytes < DIHOS_PROCESS_MIN_MEMORY_BYTES ||
        (desc->capabilities & ~DIHOS_PROCESS_CAP_KNOWN))
        return 0;
    /* Mesa receives precisely enough authority to operate through the kernel
     * graphics service.  It never obtains display or device register access. */
    if (desc->kind == DIHOS_PROCESS_KIND_RENDERER_SERVICE &&
        (desc->capabilities & (DIHOS_PROCESS_CAP_BUNDLE_READ |
                               DIHOS_PROCESS_CAP_IPC |
                               DIHOS_PROCESS_CAP_GFX_RENDERER)) !=
            (DIHOS_PROCESS_CAP_BUNDLE_READ | DIHOS_PROCESS_CAP_IPC |
             DIHOS_PROCESS_CAP_GFX_RENDERER))
        return 0;
    return 1;
}

void dihos_process_init(dihos_process_table *table)
{
    if (table)
        *table = (dihos_process_table){.next_address_space_id = 1u};
}

int dihos_process_create(dihos_process_table *table,
                         const dihos_process_desc *desc,
                         dihos_process_handle *out_handle)
{
    dihos_process_slot *slot;

    if (!table || !out_handle || !process_desc_valid(desc))
        return -1;
    for (uint32_t i = 0u; i < DIHOS_PROCESS_MAX; ++i)
    {
        slot = &table->slots[i];
        if (slot->active)
            continue;
        slot->generation = (uint16_t)(slot->generation + 1u);
        if (!slot->generation)
            slot->generation = 1u;
        slot->desc = *desc;
        slot->address_space_id = table->next_address_space_id++;
        if (!slot->address_space_id)
            slot->address_space_id = table->next_address_space_id++;
        slot->active = 1u;
        slot->state = DIHOS_PROCESS_LOADING;
        *out_handle = (dihos_process_handle){(uint16_t)i, slot->generation};
        return 0;
    }
    return -2;
}

int dihos_process_set_ready(dihos_process_table *table,
                            dihos_process_handle handle)
{
    uint32_t index;

    if (process_slot_index(table, handle, &index) != 0)
        return -1;
    if (table->slots[index].state != DIHOS_PROCESS_LOADING)
        return -2;
    table->slots[index].state = DIHOS_PROCESS_READY;
    return 0;
}

int dihos_process_set_running(dihos_process_table *table,
                              dihos_process_handle handle)
{
    uint32_t index;

    if (process_slot_index(table, handle, &index) != 0)
        return -1;
    if (table->slots[index].state != DIHOS_PROCESS_READY)
        return -2;
    table->slots[index].state = DIHOS_PROCESS_RUNNING;
    return 0;
}

int dihos_process_exit(dihos_process_table *table,
                       dihos_process_handle handle)
{
    uint32_t index;

    if (process_slot_index(table, handle, &index) != 0)
        return -1;
    if (table->slots[index].state != DIHOS_PROCESS_RUNNING &&
        table->slots[index].state != DIHOS_PROCESS_READY)
        return -2;
    table->slots[index].state = DIHOS_PROCESS_EXITED;
    table->slots[index].desc.capabilities = 0u;
    return 0;
}

int dihos_process_fault(dihos_process_table *table,
                        dihos_process_handle handle)
{
    uint32_t index;

    if (process_slot_index(table, handle, &index) != 0)
        return -1;
    table->slots[index].state = DIHOS_PROCESS_FAULTED;
    table->slots[index].desc.capabilities = 0u;
    return 0;
}

int dihos_process_reap(dihos_process_table *table,
                       dihos_process_handle handle)
{
    uint32_t index;
    uint16_t generation;

    if (process_slot_index(table, handle, &index) != 0)
        return -1;
    if (table->slots[index].state != DIHOS_PROCESS_EXITED &&
        table->slots[index].state != DIHOS_PROCESS_FAULTED)
        return -2;
    generation = table->slots[index].generation;
    table->slots[index] = (dihos_process_slot){.generation = generation};
    return 0;
}

int dihos_process_query(const dihos_process_table *table,
                        dihos_process_handle handle,
                        dihos_process_info *out_info)
{
    uint32_t index;
    const dihos_process_slot *slot;

    if (!out_info || process_slot_index(table, handle, &index) != 0)
        return -1;
    slot = &table->slots[index];
    *out_info = (dihos_process_info){handle, slot->desc.kind, slot->state,
                                     slot->address_space_id,
                                     slot->desc.memory_limit_bytes,
                                     slot->desc.capabilities};
    return 0;
}
