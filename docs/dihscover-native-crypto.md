# Native WebCore cryptography

OpenSSL 3.5.9 now builds as an ARM64 DihOS `libcrypto` and executes inside
the production EL0 runtime. The [recorded validation](browser-evaluation/native-crypto-20261101.json)
contains 30 passing ARM64 QEMU groups, including both native crypto cases.
The [combined backend validation](browser-evaluation/native-crypto-backend-20261101.json)
also passes all 48 regression groups with the existing JSC, resource loading,
graphics and font fixtures. Eight actual WebCore/PAL crypto units and the
offscreen Page client compile in the OpenSSL-enabled configuration.
The installed browser, working kernel binary and VHD have not been updated.

The positive executable verifies SHA-256, RFC 4231 HMAC-SHA256, NIST AES-GCM
encryption/decryption and RFC 8032 Ed25519 signatures. It rejects modified
authentication tags/signatures, exercises the native entropy/DRBG path,
blocks and wakes two DihOS worker threads, checks once initialization and
thread-local isolation/destructors, then reclaims every process page.
The separate negative executable requires random generation to fail when
trusted entropy is unavailable. The harness owns the unavailable/seeded
entropy states; these runs make no hardware entropy-quality claim.

Both executables pass the production ELF loader with zero unresolved imports.
The positive image is 2,682,880 bytes, has 21,995 relative relocations and
868 TLS bytes. Existing loader limits have not been increased.

## Native boundary

OpenSSL uses its upstream thread/RCU implementation with its supported mutex
fallback. A private `pthread.h` adapter maps the required operations to SACX
mutexes, conditions, once initialization and generation-checked TLS keys.
It is not installed as a public POSIX API. Synchronization uses actual
wait/wake operations; lifecycle callbacks run through the userspace runtime.

The optional internal OpenSSL worker pool is disabled. Its generic unsupported
backend otherwise makes library-context initialization fail on this new OS.
Thread safety remains enabled, and DihOS application threads call OpenSSL
concurrently in the test. Assembly, asynchronous engine jobs, dynamic modules,
socket/stdio/POSIX I/O, automatic configuration loading and secure-heap page
locking are excluded from this crypto profile. Environment-selected providers
and configuration are refused. This is not a port of OpenSSL's TLS/socket layer;
DihOS networking continues to use its existing transport/TLS implementation.

The seed-pool adapter credits only successful SACX CSPRNG output. It cleanses
failed buffers, bounds individual pool requests, and never substitutes a clock
or address for entropy. Sleep uses bounded SACX sleep calls, and OpenSSL's
wall-clock helper reads the real SACX UTC service with explicit error handling.
Private file-descriptor/metadata adapters return access-denied errors. The
small additional C helpers implement the runtime's fixed C-locale contract.

## Reproduction

The [publisher's release list](https://openssl-library.org/source/) links the
pinned source and checksum:
`603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a`.

From the checkout:

```powershell
python tools/webcore_dihos/prepare_openssl.py
```

In ARM64 WSL:

```sh
python3 tools/webcore_dihos/build_openssl.py
python3 tools/webcore_dihos/build_crypto_fixture.py
python3 tools/webcore_dihos/build_crypto_fixture.py --unready
```

Then in PowerShell, using the configured Python executable if necessary:

```powershell
.\tools\test_user_runtime_qemu.ps1 -OpenSsl -ReusePrograms
```

`-OpenSsl` selects test-owned entropy and cannot be combined with
`-RequireRndr`; run the architectural RNDR test separately. Other regression
profiles retain their existing entropy configuration.

Build directories are keyed to the adapter/recipe contents. The public
OpenSSL `build_libs` target performs dependency regeneration, restricted to
`LIBS=libcrypto.a`. Calling the raw archive target bypasses that regeneration
and can retain stale objects. Reports record library, adapter, SDK, compiler
wrapper and executable hashes. Runtime validation remains separate from a
successful build or link.

## WebCore integration

`configure_native.py --openssl` creates a separate font-enabled diagnostic
profile; adding `--engine` selects its unified full-engine build. Configuration
requires the passing native crypto validation and matching archive/source
hashes. It imports the actual native archive, enables `USE_OPENSSL`, includes
WebCore's upstream OpenSSL source set and adds PAL's OpenSSL digest backend.
No Linux/Windows crypto library is linked into the guest.

The pinned WebCore RSA backend needs const-correct read-only pointers for
OpenSSL 3's `EVP_PKEY_get0_RSA` API, including its modulus helper. Those edits
and the network-field decoder live in `engine-edits.json`; the shared WTF
event-loop manifest retains its original bytes, keeping the previously
validated event-loop fixtures reproducible.

The complete WebCore archive, Page link/execution, window presentation and
live JavaScript sites still need validation. The successful standalone crypto
tests do not enable JavaScript webpages in the installed NetSurf browser.
