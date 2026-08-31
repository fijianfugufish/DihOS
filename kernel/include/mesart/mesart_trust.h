#pragma once

#include <stdint.h>

/*
 * This is not SACX metadata and it deliberately has no compatibility path to
 * the SACX loader.  A signed manifest is the sole admission record for the
 * EL0 Mesa renderer service and its GPU-visible assets.
 */

#define MESART_MANIFEST_MAGIC          0x5452534du /* "MSRT" */
#define MESART_MANIFEST_VERSION        1u
#define MESART_MANIFEST_MAX_FILES      32u
#define MESART_MANIFEST_PATH_BYTES     160u
#define MESART_SHA256_BYTES            32u
#define MESART_P256_PUBLIC_KEY_BYTES   65u
#define MESART_P256_SIGNATURE_BYTES    64u /* raw r || s, big-endian */

typedef enum mesart_manifest_role
{
    MESART_ROLE_RENDERER_SERVICE = 1,
    MESART_ROLE_ADRENO_GMU_FIRMWARE = 2,
    MESART_ROLE_ADRENO_SQE_FIRMWARE = 3,
    MESART_ROLE_ADRENO_ZAP_FIRMWARE = 4,
    MESART_ROLE_KERNEL_COMPOSITOR_SHADER = 5,
} mesart_manifest_role;

/* All multibyte fields are little-endian.  The signature is appended after
 * the final entry and signs exactly header_bytes + file_count * entry_bytes. */
typedef struct __attribute__((packed)) mesart_manifest_header
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_bytes;
    uint32_t runtime_abi;
    uint32_t file_count;
    uint64_t capability_mask;
    uint8_t bundle_id[16];
} mesart_manifest_header;

typedef struct __attribute__((packed)) mesart_manifest_file
{
    uint32_t role;
    uint32_t flags;
    uint64_t bytes;
    uint8_t sha256[MESART_SHA256_BYTES];
    /* NUL-terminated, normalized relative path below the verified root. */
    char path[MESART_MANIFEST_PATH_BYTES];
} mesart_manifest_file;

typedef struct mesart_trust_root
{
    /* SEC1 uncompressed P-256 point: 0x04 || X || Y. */
    uint8_t public_key[MESART_P256_PUBLIC_KEY_BYTES];
} mesart_trust_root;

typedef struct mesart_manifest_view
{
    const mesart_manifest_header *header;
    const mesart_manifest_file *files;
    const uint8_t *signature;
    uint64_t signed_bytes;
    uint64_t total_bytes;
} mesart_manifest_view;

/* Checks manifest framing, every path, and its P-256 signature against the
 * root supplied by the kernel build.  It does not access the filesystem. */
int mesart_manifest_verify(const void *manifest, uint64_t manifest_bytes,
                           const mesart_trust_root *root,
                           mesart_manifest_view *out_view);

/* Streams each declared file from bundle_root, checks exact length and
 * SHA-256, and rejects the first mismatch.  bundle_root itself is kernel
 * controlled (for example, "0:/OS/MesaRuntime"). */
int mesart_manifest_verify_files(const mesart_manifest_view *view,
                                 const char *bundle_root);

