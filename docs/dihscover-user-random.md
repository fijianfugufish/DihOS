# Kernel-backed randomness for native engine processes

The native bmalloc and WTF random interfaces now call a permission-checked SACX
service. They use the existing BearSSL HMAC-DRBG with SHA-256 directly, rather
than the port's POSIX file fallback or upstream userspace RC4 stream. No clock,
MAC address, pointer or packet timing is credited as entropy.

## Sources and handoff

Stage 2 captures 48 bytes through the [UEFI RNG protocol](https://uefi.org/specs/UEFI/2.11/37_Secure_Technologies.html)
before ExitBootServices. It tries the raw algorithm first and uses the default
algorithm only after EFI_UNSUPPORTED. Other firmware errors remain failures.
The specification requires at least 256-bit security for its default DRBG.
The caller supplies the output buffer; no firmware RNG call occurs in the kernel.

Boot-info version 3 appends the seed, validity/source flags and EFI status.
Earlier offsets stay unchanged. The original stage-2 seed is wiped after copying
the handoff; the kernel consumes and wipes its handoff copy before general
allocation. Seed bytes never enter stage-2 diagnostic text or the disk snapshot.
Grossly uniform or failed firmware output is not accepted. This check does not
certify a firmware entropy source's quality: the firmware/provider is trusted.

On ARM64, the kernel also detects the architectural RNDR feature through
ID_AA64ISAR0_EL1. It reads RNDR only when advertised, validates its success
condition, bounds retries and rejects an obviously stuck repeated-word source.
Unsupported/unavailable sources do not fall back to a guessed software seed.

The generator opportunistically refreshes from RNDR after 65,536 requests.
A temporarily unavailable refresh does not exhaust an otherwise seeded DRBG.
The hard limit is 2^48 Generate calls between reseeds, following the
[BearSSL HMAC-DRBG contract](https://bearssl.org/apidoc/bearssl__rand_8h.html).
At that limit a failed reseed returns an error. Firmware-only boots remain usable
without a small global budget that an application could trivially exhaust.

## Userspace boundary

ABI v1 appends operation 17, `SACX_USER_RANDOM`, and a separate capability bit 8.
QUERY advertises mask `0x3ffff`. The development ELF loader grants the new bit;
this is not a completed application permission-policy UI.

Each syscall accepts at most 256 bytes. The kernel checks grant, version, flags,
length and the entire writable owned destination before generating anything.
No source/not ready returns `-11`; denied access and invalid buffers remain
errors. A kernel-local temporary is wiped after copying. An atomic try-lock
serializes generator state without spinning on an interrupted owner.

`sacx_user_random` splits larger requests into those bounded calls. It never
waits for entropy. On an error, discard the entire output: an earlier chunk may
have been filled. The void WebKit interfaces terminate only their process when
trusted random output cannot be obtained. Zero-length library requests are no-ops.
The native integer and unit-interval APIs preserve the upstream value ranges.

`hv:diag` reports readiness, sources, architectural feature availability, last
error, request/reseed counts and firmware EFI status. Source bits are firmware
1, raw firmware 2 and RNDR 4. These fields contain no seed or DRBG key/state.

## Validation

- Host tests pass the upstream BearSSL known-answer vector and an independently
  calculated 48-byte-seed HMAC/SHA-256 output, seed erasure, unavailable/stuck
  hardware, bounded retries, soft refresh recovery and hard reseed boundaries.
- Firmware mocks cover raw/default selection, NOT_READY, missing/malformed
  providers, output erasure and the caller-owned buffer ABI.
- SACX tests reject missing grants, bad flags, excessive sizes and invalid
  buffers before invoking the random provider; unavailable entropy leaves output
  untouched.
- ARM64 EL0 tests pass unseeded SDK errors, protected output rejection, bounded
  chunking, and actual bmalloc/WTF positive and fail-closed paths, with repeated
  cleanup. Their deterministic fixture seed is test-only, not a production seed.
- QEMU's Cortex-A72 path covers absent RNDR. Its max-CPU run executes the actual
  RNDR instruction path and checks source credit. This tests instruction handling,
  not physical source quality. Existing runtime/VM/RTTI/large-image tests pass.
- ARM64 stage-2 capture/handoff, kernel startup and Hyper-V diagnostics compile.

[Retained evidence](browser-evaluation/native-random-20261029.json) records the
actual adapter ELF/source hashes and both QEMU log hashes, with physical-source
validation explicitly false.

From PowerShell:

```powershell
.\tools\test_entropy.ps1
```

From the repository in ARM64 WSL, after preparing the pinned engine dependencies:

```sh
python3 tools/jsc_dihos_probe.py --inventory --icu build/icu-dihos/stage-report.json
python3 tools/build_webkit_random_test.py
```

Then run the integration suite:

```powershell
.\tools\test_user_runtime_qemu.ps1 -WebkitRandom -Python <python-executable>
.\tools\test_user_runtime_qemu.ps1 -WebkitRandom -Cpu max -RequireRndr -Python <python-executable>
```

`-Rtti` and `-WebkitVm` also exercise those optional prebuilt fixtures. The
standard random fixtures are included by default. The actual WebKit fixture
exits `0x5e` when seeded and `185` when unseeded; the SDK positive fixture exits
`0x5d`. The unready fixture is harness-specific and is not expected to pass after
a normal, successfully seeded boot.

## Remaining gates

The [reported Hyper-V fresh-boot test](browser-evaluation/hyperv-random-test.md)
passes: firmware and RNDR sources are available (`0x5`), firmware status is zero,
and the SDK fixture exits `0x5d` with its heap reclaimed. The separate actual
WebKit adapters remain QEMU-tested; native hardware still needs validation.
VM snapshot/clone/resume reseeding is not integrated with a generation-change
notification yet. Use fresh boots for this prototype; copied saved generator
state does not provide independent streams. This is not a FIPS validation or a
complete OS cryptography audit. The existing TLS transport still has its
separate provisional entropy helper; this change does not claim to harden it.

The full JSC build now passes the old random-source `open`/`read` failure and
reaches the native thread/register-context gap. The engine still needs those
contracts, floating-point environment, clocks and other runtime services before
it can run inside DihOS. Only the test image was updated; the working browser
and working-tree kernel binary remain unchanged.
