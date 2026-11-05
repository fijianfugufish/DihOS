# Userspace arena allocator for the JavaScriptCore runtime

The isolated C/C++ runtime now suballocates objects with the existing vendored
TLSF 3.1 implementation. Warm allocation/free operations use private user memory
without one SACX reservation per object. Pool growth and release still cross the
checked SACX boundary; no kernel pointers or physical addresses are exposed.

## Contract and limits

The runtime exports `malloc`, `free`, `calloc`, `realloc`, `aligned_alloc`, and
ordinary/sized/aligned C++ `new`/`delete`. The internal header
`sdk/sacx/user_runtime/include/dihos_user_allocator.h` also exposes integrity,
statistics and trimming functions. The subsequent [native thread milestone](dihscover-user-threads.md)
adds acquire/release allocator locking with SACX wait/wake on contention. The
original arena package below predates that synchronization.

Each pool is a 256 KiB SACX-owned region. Up to eight pools provide at most 2 MiB
of backing, including TLSF metadata and alignment gaps. Individual allocations
are capped at 240 KiB. Fundamental alignment is 16 bytes; power-of-two alignment
up to 64 KiB is supported when the padded request fits. For example, a 64 KiB
allocation aligned to 64 KiB fits, while a 128 KiB allocation with that alignment
does not fit TLSF's padded request in these pools. Impossible requests fail before
pool acquisition rather than consuming eight unusable reservations.

`malloc(0)` returns null. `calloc` checks multiplication overflow and zeroes the
requested bytes. Reallocation preserves data and leaves the old allocation
untouched on failure; shrinking keeps its existing capacity. Moved allocations
retain fundamental alignment. `aligned_alloc` additionally requires size to be a
multiple of alignment. C allocation failure returns null; the no-exceptions C++
runtime terminates that app on failed `new`. No POSIX errno/thread-local runtime
is claimed. Normal C pointer-lifetime rules apply to free/realloc.

Freed blocks remain available for reuse. Explicit trim releases idle secondary
pools, and releases the control pool too when every allocation is freed. Normal
C++ startup finalizes destructors before trimming. Stop/fault/exit cleanup still
belongs to the kernel and reclaims every owned reservation, including leaked
objects. Allocator statistics distinguish live block bytes, peak live bytes,
current live allocations, pools and reserved backing.

TLSF's native build path is unchanged by default. A small conditional include
selects the freestanding platform adapter only with `DIHOS_USER_TLSF`; assertions
remain active. Diagnostics support TLSF's actual formatting conversions. The
adapter provides no success-returning I/O or threading stubs. TLSF's complete
licence is packaged under `UserRuntime/Licenses/TLSF.txt` and its source hashes
are retained with the app build recipe.

## Verification

The actual adapter and TLSF pass native ARM64 Linux AddressSanitizer (including
leak checks) and UndefinedBehaviorSanitizer with halt-on-error. Tests cover:

- 1,024 live objects in one pool and warm reuse without further pool acquisition;
- alignment through 64 KiB, overflow, zeroing and failed-realloc preservation;
- 30,000 deterministic randomized allocation/free/resize operations with data and
  TLSF-integrity checks, including trim while other pools remain live;
- all eight pools, exhaustion/reuse, failed backing acquisition and pool-boundary
  requests, with all mock backing released at the end.

The ARM64 QEMU fixture uses the real SACX, page-table, scheduler and heap-service
code. It verifies the C allocation wrappers and over-aligned C++ `new`/`delete`,
1,024 objects in one pool, all eight real regions and complete trimming. The
earlier C++/heap/fault/isolation tests remain. The test arena grows to 4 MiB so
the full 2 MiB pool limit can be exercised; production heap limits are unchanged.

```powershell
wsl -d Ubuntu -- sh tools/test_user_allocator_host.sh
.\tools\test_user_runtime_qemu.ps1 -Python python -TimerPageOffset -FirmwareL2 -FirmwareL0
```

Prepared package: `build/dihos-user-arena-20261028`, build
`20261028T173117Z-1D1D3EF6F61A`. The isolated ARM64 build, sanitized host tests,
both QEMU layouts, malformed-ELF tests, Hyper-V host and deployment tests passed.
All 64 manifest files/hashes were verified, including TLSF's licence. Exact
source hashes, test logs and package results are in `build/user-validation-arena`.
The VHDX has not been modified.

With the VM off, update from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-arena-20261028
```

Then boot and run:

```text
proc:run 0:/OS/System/Programs/UserRuntime/arena.elf
proc:status
flush
```

Allow a few frames before status. Expected: **exited**, status `0x4C`, heap peak
`0x200000`, zero reserved/committed bytes after cleanup, and:

```text
SACX arena: 1024 objects, alignment, realloc and exhaustion OK
```

The user confirmed this arena fixture on Hyper-V: 57 SACX calls, exit `0x4C`,
the expected success message, zero reserved/committed bytes and peak `0x200000`.
It removes the
one-region-per-object limitation; it does not make JavaScriptCore's larger heap,
full target libc/C++/ICU, threads/TLS/waits or platform adapter available yet.
The next [selected libc++ container/string slice](dihscover-user-library.md)
passes ARM64 QEMU; its guest test remains pending.
