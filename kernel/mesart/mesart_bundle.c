#include "mesart/mesart_bundle.h"

#include "kwrappers/kfile.h"
#include "memory/pmem.h"

#define MESART_BUNDLE_PATH_MAX 320u
#define MESART_BUNDLE_PAGE_BYTES 4096ull

static int make_manifest_path(char out[MESART_BUNDLE_PATH_MAX],
                              const char *bundle_root)
{
    uint32_t at = 0u;

    if (!out || !bundle_root)
        return -1;
    while (bundle_root[at])
    {
        if (at + 1u >= MESART_BUNDLE_PATH_MAX)
            return -2;
        out[at] = bundle_root[at];
        ++at;
    }
    if (!at)
        return -3;
    if (out[at - 1u] != '/')
        out[at++] = '/';
    for (uint32_t i = 0u; MESART_BUNDLE_MANIFEST_NAME[i]; ++i)
    {
        if (at + 1u >= MESART_BUNDLE_PATH_MAX)
            return -4;
        out[at++] = MESART_BUNDLE_MANIFEST_NAME[i];
    }
    out[at] = '\0';
    return 0;
}

void mesart_bundle_release(mesart_verified_bundle *bundle)
{
    if (!bundle)
        return;
    if (bundle->manifest_memory && bundle->manifest_pages)
        pmem_free_pages(bundle->manifest_memory, bundle->manifest_pages);
    *bundle = (mesart_verified_bundle){0};
}

const mesart_manifest_file *mesart_bundle_renderer_file(
    const mesart_verified_bundle *bundle)
{
    if (!bundle || !bundle->manifest.header || !bundle->manifest.files ||
        bundle->renderer_file_index >= bundle->manifest.header->file_count)
        return 0;
    return &bundle->manifest.files[bundle->renderer_file_index];
}

int mesart_bundle_open(const char *bundle_root, const mesart_trust_root *root,
                       mesart_verified_bundle *out_bundle)
{
    mesart_verified_bundle bundle = {0};
    KFile file;
    char manifest_path[MESART_BUNDLE_PATH_MAX];
    uint64_t bytes;
    uint32_t read = 0u;
    int rc;

    if (!bundle_root || !root || !out_bundle ||
        make_manifest_path(manifest_path, bundle_root) != 0 ||
        kfile_open(&file, manifest_path, KFILE_READ) != 0)
        return -1;
    bytes = kfile_size(&file);
    if (!bytes || bytes > MESART_BUNDLE_MANIFEST_MAX_BYTES)
    {
        kfile_close(&file);
        return -2;
    }
    bundle.manifest_pages = (bytes + MESART_BUNDLE_PAGE_BYTES - 1u) /
                            MESART_BUNDLE_PAGE_BYTES;
    bundle.manifest_memory = pmem_alloc_pages(bundle.manifest_pages);
    if (!bundle.manifest_memory ||
        kfile_read(&file, bundle.manifest_memory, (uint32_t)bytes, &read) != 0 ||
        read != bytes)
    {
        kfile_close(&file);
        mesart_bundle_release(&bundle);
        return -3;
    }
    kfile_close(&file);
    rc = mesart_manifest_verify(bundle.manifest_memory, bytes, root,
                                &bundle.manifest);
    if (rc != 0)
    {
        mesart_bundle_release(&bundle);
        return -4;
    }
    rc = mesart_manifest_verify_files(&bundle.manifest, bundle_root);
    if (rc != 0)
    {
        mesart_bundle_release(&bundle);
        return -5;
    }
    for (uint32_t i = 0u; i < bundle.manifest.header->file_count; ++i)
        if (bundle.manifest.files[i].role == MESART_ROLE_RENDERER_SERVICE)
        {
            bundle.renderer_file_index = i;
            *out_bundle = bundle;
            return 0;
        }
    mesart_bundle_release(&bundle);
    return -6;
}
