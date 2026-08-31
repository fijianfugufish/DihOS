#pragma once

#include <stdint.h>
#include "mesart/mesart_trust.h"

#define MESART_BUNDLE_MANIFEST_NAME "manifest.msrt"
#define MESART_BUNDLE_MANIFEST_MAX_BYTES (64u * 1024u)

typedef struct mesart_verified_bundle
{
    void *manifest_memory;
    uint64_t manifest_pages;
    mesart_manifest_view manifest;
    uint32_t renderer_file_index;
} mesart_verified_bundle;

/* Reads a fixed manifest location below bundle_root, then verifies its
 * signature and every listed file before returning any service information.
 * No executable data is mapped or started by this layer. */
int mesart_bundle_open(const char *bundle_root, const mesart_trust_root *root,
                       mesart_verified_bundle *out_bundle);
void mesart_bundle_release(mesart_verified_bundle *bundle);
const mesart_manifest_file *mesart_bundle_renderer_file(
    const mesart_verified_bundle *bundle);

