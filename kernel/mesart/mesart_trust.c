#include "mesart/mesart_trust.h"

#include <stddef.h>
#include <bearssl.h>

#include "kwrappers/kfile.h"

#define MESART_VERIFY_CHUNK_BYTES 1024u

static uint32_t equal_bytes(const uint8_t *a, const uint8_t *b, uint32_t count)
{
    uint8_t difference = 0u;

    for (uint32_t i = 0u; i < count; ++i)
        difference |= (uint8_t)(a[i] ^ b[i]);
    return difference == 0u;
}

static int path_length_and_validate(const char path[MESART_MANIFEST_PATH_BYTES],
                                    uint32_t *out_length)
{
    uint32_t segment_start = 0u;
    uint32_t length;

    if (!path || !out_length || !path[0] || path[0] == '/' || path[0] == '.')
        return -1;
    for (length = 0u; length < MESART_MANIFEST_PATH_BYTES; ++length)
    {
        char c = path[length];
        if (!c)
            break;
        if ((unsigned char)c < 0x20u || c == '\\' || c == ':' ||
            !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '/' || c == '.' ||
              c == '_' || c == '-'))
            return -2;
        if (c == '/')
        {
            if (length == segment_start ||
                (length == segment_start + 2u && path[segment_start] == '.' &&
                 path[segment_start + 1u] == '.'))
                return -3;
            segment_start = length + 1u;
        }
    }
    if (length == MESART_MANIFEST_PATH_BYTES || length == segment_start ||
        (length == segment_start + 2u && path[segment_start] == '.' &&
         path[segment_start + 1u] == '.'))
        return -4;
    *out_length = length;
    return 0;
}

static uint32_t same_path(const char *a, const char *b)
{
    for (uint32_t i = 0u; i < MESART_MANIFEST_PATH_BYTES; ++i)
    {
        if (a[i] != b[i])
            return 0u;
        if (!a[i])
            return 1u;
    }
    return 0u;
}

static int validate_entries(const mesart_manifest_file *files,
                            uint32_t file_count)
{
    uint32_t service_count = 0u;

    for (uint32_t i = 0u; i < file_count; ++i)
    {
        uint32_t path_length;
        if (files[i].role < MESART_ROLE_RENDERER_SERVICE ||
            files[i].role > MESART_ROLE_KERNEL_COMPOSITOR_SHADER ||
            !files[i].bytes ||
            path_length_and_validate(files[i].path, &path_length) != 0)
            return -1;
        (void)path_length;
        if (files[i].role == MESART_ROLE_RENDERER_SERVICE)
            ++service_count;
        for (uint32_t j = 0u; j < i; ++j)
            if (same_path(files[i].path, files[j].path))
                return -2;
    }
    return service_count == 1u ? 0 : -3;
}

int mesart_manifest_verify(const void *manifest, uint64_t manifest_bytes,
                           const mesart_trust_root *root,
                           mesart_manifest_view *out_view)
{
    const uint8_t *bytes = (const uint8_t *)manifest;
    const mesart_manifest_header *header;
    const mesart_manifest_file *files;
    const uint8_t *signature;
    br_sha256_context hash;
    br_ec_public_key key;
    uint8_t digest[MESART_SHA256_BYTES];
    uint64_t signed_bytes;

    if (!bytes || !root || !out_view ||
        manifest_bytes < sizeof(mesart_manifest_header) +
                         MESART_P256_SIGNATURE_BYTES)
        return -1;
    header = (const mesart_manifest_header *)bytes;
    if (header->magic != MESART_MANIFEST_MAGIC ||
        header->version != MESART_MANIFEST_VERSION ||
        header->header_bytes != sizeof(*header) ||
        !header->runtime_abi || !header->file_count ||
        header->file_count > MESART_MANIFEST_MAX_FILES)
        return -2;
    signed_bytes = sizeof(*header) +
                   (uint64_t)header->file_count * sizeof(*files);
    if (signed_bytes > manifest_bytes ||
        manifest_bytes != signed_bytes + MESART_P256_SIGNATURE_BYTES)
        return -3;
    files = (const mesart_manifest_file *)(bytes + sizeof(*header));
    if (validate_entries(files, header->file_count) != 0)
        return -4;
    signature = bytes + signed_bytes;
    if (root->public_key[0] != 0x04u)
        return -5;
    key = (br_ec_public_key){BR_EC_secp256r1, (unsigned char *)root->public_key,
                             MESART_P256_PUBLIC_KEY_BYTES};
    br_sha256_init(&hash);
    br_sha256_update(&hash, bytes, (size_t)signed_bytes);
    br_sha256_out(&hash, digest);
    if (!br_ecdsa_i31_vrfy_raw(&br_ec_prime_i31, digest, sizeof(digest),
                               &key, signature,
                               MESART_P256_SIGNATURE_BYTES))
        return -6;
    *out_view = (mesart_manifest_view){header, files, signature, signed_bytes,
                                       manifest_bytes};
    return 0;
}

static int join_bundle_path(char out[320], const char *root,
                            const char relative[MESART_MANIFEST_PATH_BYTES])
{
    uint32_t offset = 0u;
    uint32_t relative_length;

    if (!out || !root || path_length_and_validate(relative, &relative_length) != 0)
        return -1;
    while (root[offset])
    {
        if (offset >= 158u)
            return -2;
        out[offset] = root[offset];
        ++offset;
    }
    if (!offset)
        return -3;
    if (out[offset - 1u] != '/')
        out[offset++] = '/';
    if ((uint64_t)offset + relative_length >= 320u)
        return -4;
    for (uint32_t i = 0u; i < relative_length; ++i)
        out[offset + i] = relative[i];
    out[offset + relative_length] = '\0';
    return 0;
}

int mesart_manifest_verify_files(const mesart_manifest_view *view,
                                 const char *bundle_root)
{
    uint8_t chunk[MESART_VERIFY_CHUNK_BYTES];

    if (!view || !view->header || !view->files || !bundle_root)
        return -1;
    for (uint32_t i = 0u; i < view->header->file_count; ++i)
    {
        KFile file;
        br_sha256_context hash;
        uint8_t digest[MESART_SHA256_BYTES];
        char path[320];
        uint64_t remaining;

        if (join_bundle_path(path, bundle_root, view->files[i].path) != 0 ||
            kfile_open(&file, path, KFILE_READ) != 0)
            return -2;
        remaining = kfile_size(&file);
        if (remaining != view->files[i].bytes)
        {
            kfile_close(&file);
            return -3;
        }
        br_sha256_init(&hash);
        while (remaining)
        {
            uint32_t got = 0u;
            uint32_t want = remaining > sizeof(chunk) ? sizeof(chunk) :
                                                        (uint32_t)remaining;
            if (kfile_read(&file, chunk, want, &got) != 0 || got != want)
            {
                kfile_close(&file);
                return -4;
            }
            br_sha256_update(&hash, chunk, got);
            remaining -= got;
        }
        kfile_close(&file);
        br_sha256_out(&hash, digest);
        if (!equal_bytes(digest, view->files[i].sha256, sizeof(digest)))
            return -5;
    }
    return 0;
}
