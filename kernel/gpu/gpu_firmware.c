#include "gpu/gpu_firmware.h"
#include "kwrappers/kfile.h"

#define GPU_FIRMWARE_PATH_MAX 192u

static uint32_t append(char *dst, uint32_t at, uint32_t cap, const char *src)
{
    while (*src && at + 1u < cap)
        dst[at++] = *src++;
    return at;
}

static int make_path(const gpu_firmware_manifest *manifest, const char *name,
                     char path[GPU_FIRMWARE_PATH_MAX])
{
    uint32_t at = 0u;
    if (!manifest || !manifest->directory || !name) return -1;
    at = append(path, at, GPU_FIRMWARE_PATH_MAX, manifest->directory);
    if (at && path[at - 1u] != '/' && at + 1u < GPU_FIRMWARE_PATH_MAX)
        path[at++] = '/';
    at = append(path, at, GPU_FIRMWARE_PATH_MAX, name);
    if (!at || at + 1u >= GPU_FIRMWARE_PATH_MAX) return -1;
    path[at] = '\0';
    return 0;
}

int gpu_firmware_scan(const gpu_firmware_manifest *manifest,
                      gpu_firmware_inventory *out)
{
    gpu_firmware_inventory inventory = {0};

    if (!manifest || !manifest->directory || !manifest->files || !out)
        return -1;

    for (uint32_t i = 0; i < manifest->file_count; ++i)
    {
        char path[GPU_FIRMWARE_PATH_MAX];
        KFile file;
        int present;

        if (manifest->files[i].required)
            ++inventory.required_count;

        if (make_path(manifest, manifest->files[i].name, path) != 0)
            return -1;

        present = (kfile_open(&file, path, KFILE_READ) == 0);
        if (!present)
            continue;
        kfile_close(&file);
        if (manifest->files[i].required)
            ++inventory.present_count;
        else
            ++inventory.optional_present_count;
    }

    *out = inventory;
    return inventory.present_count == inventory.required_count ? 0 : -2;
}

void gpu_firmware_release(gpu_firmware_set *set)
{
    if (!set) return;
    for (uint32_t i = 0; i < set->blob_count; ++i)
        gpu_buffer_release(&set->blobs[i].buffer);
    *set = (gpu_firmware_set){0};
}

int gpu_firmware_load(const gpu_firmware_manifest *manifest,
                      gpu_firmware_set *out)
{
    gpu_firmware_set set = {0};
    if (!manifest || !out || manifest->file_count > GPU_FIRMWARE_MAX_BLOBS)
        return -1;

    for (uint32_t i = 0; i < manifest->file_count; ++i)
    {
        const gpu_firmware_file *desc = &manifest->files[i];
        gpu_firmware_blob *blob = &set.blobs[set.blob_count];
        char path[GPU_FIRMWARE_PATH_MAX];
        KFile file;
        uint32_t read = 0u;
        uint64_t bytes;

        if (make_path(manifest, desc->name, path) != 0 ||
            kfile_open(&file, path, KFILE_READ) != 0)
        {
            if (desc->required) goto fail;
            continue;
        }
        bytes = kfile_size(&file);
        if (!bytes || bytes > 0xffffffffull ||
            (desc->expected_size && bytes != desc->expected_size) ||
            gpu_buffer_alloc(&blob->buffer, bytes,
                             GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0)
        {
            kfile_close(&file);
            goto fail;
        }
        blob->role = desc->role;
        ++set.blob_count;
        if (kfile_read(&file, blob->buffer.cpu, (uint32_t)bytes, &read) != 0 ||
            read != bytes)
        {
            kfile_close(&file);
            goto fail;
        }
        kfile_close(&file);
        gpu_buffer_prepare_for_device(&blob->buffer);
        set.total_bytes += bytes;
    }
    *out = set;
    return 0;
fail:
    gpu_firmware_release(&set);
    return -2;
}
