# Larger static executables and guarded heaps

This removes the first obvious size barriers to a native JavaScriptCore port.
The pinned ARM64 Linux JSC shell is about 18.9 MB; it is **not** a DihOS binary.
The new fixture is a real DihOS static ARM64 PIE with a 20 MiB initialized
payload, more than 2,048 relative relocations and over 24 MiB of mapped image.
No Linux imports, dynamic linker, JavaScript engine or JIT are introduced here.

## Limits and ownership

| Resource | Current bring-up limit |
|---|---|
| ELF file | 32 MiB |
| Image span, including BSS/holes | 64 MiB |
| Relative fixups | 32,768; only symbol-free R_AARCH64_RELATIVE |
| Segments / section headers | 8 / 512 |
| Private page tables | 192 pages maximum, precharged to process quota |
| Total process quota | 128 MiB, including image/stack guards and table allowance |
| Heap reservations | 16 guarded regions, each at most 16 MiB |
| Commit/protect batch | 64 KiB |
| malloc request | At most 8 MiB, alignment at most 64 KiB |
| User stack / TLS (unchanged) | 16 KiB / at most 4,080 template bytes |

Reservations still eagerly own contiguous physical backing. Page tables are
prepared only when pages are committed; uncommitted pages retain inherited
EL1-only mappings. Every prior owner revokes user permissions before releasing
backing. Commit validates the whole batch, zeros it, sets NX permissions and
publishes tables. A 4,096-page bitmap per region handles boundaries beyond the
old single 64-bit word. Release revokes committed leaves before freeing backing.
It is bounded by the 16 MiB region cap, but is not an asynchronous decommit API.

The allocator keeps small 256 KiB pools initially and expands later pools up to
16 MiB. There are at most 16 pools, constrained by the kernel quota. Pool sizes
are accounted individually; trimming releases every idle secondary pool and the
control pool when no allocations remain. Ordinary malloc is not a zeroing API;
calloc and the raw SACX memory allocator retain their zeroing contracts.

Large relocation tables are indexed in the immutable kernel-owned ELF source,
not copied into a giant stack array. Tables larger than 256 entries must be
strictly ordered by target address, allowing linear duplicate rejection.
Small existing executables retain their inline relocation plan. Fixups cannot
target executable segments, TLS templates, foreign ranges or duplicate targets.

## Incremental image admission

The file loader reads at most 64 KiB per poll, then transfers the staging buffer
to the runtime. On transfer failure the caller still owns the buffer; on success
the runtime owns it until admission finishes or fails. No app refers to staging
or to the open file. The synchronous internal spawn entry remains limited to
256 KiB files/images, preventing accidental large synchronous admission.

Large admission performs one batch per runtime poll, fairly interleaved with
other processes. `proc:status` shows **loading** before any user instruction can
run, with an image phase/work/span diagnostic:

| Phase, shown in hex | Work |
|---|---|
| 0x15 | Validate up to 256 fixups |
| 0x16 | Allocate owned image/stack and private VM |
| 0x17 | Zero at most 64 KiB |
| 0x18 | Copy at most 64 KiB of a segment |
| 0x19 | Apply/publish up to 256 validated fixups |
| 0x1A | Map/cache-publish at most 64 KiB |
| 0x1B | Initialize main TLS/stack, publish and mark ready |

Work is a record offset in fixup phases and a byte offset within the active
image/segment in copy/map phases. Span is the whole image size, not a percentage
denominator for every phase. Source is freed before the first EL0 instruction.
The staging/image peak is checked against the quota before admission; future
worker stacks and tables are precharged before exposing a heap allowance.

`proc:stop` cancels reading, admission or execution and reclaims owned buffers,
private tables and process slots. Tests caught and fixed a loading-slot leak:
the ordinary process-exit transition does not accept a still-loading process,
so pending cancellation now invalidates and reaps that registry entry too.

Contiguous physical allocation, table publication, bounded heap release and
filesystem/driver waits still execute in kernel context. This does not claim
measured sub-100-ms guest latency or a complete asynchronous storage subsystem.
Sparse reservations, decommit, larger engine-specific alignment requirements,
dynamic linking and further C/ICU/WTF adaptation remain separate work.

## Evidence and guest test

Both ARM64 QEMU layouts exercise production VM/scheduler/syscall code. The suite
runs the 20 MiB file, verifies initialized data and zero BSS, applies thousands
of fixups and uses both raw 8 MiB memory and an 8 MiB malloc request. It tests RO
hardware faults, cancellation in every admission phase and during heap commit,
late malformed fixups, backing/stack/table allocation failures, cross-word page
protection and full 16 MiB zeroing/commit/release. All paths reclaim owned pages
and process slots. Earlier runtime/thread/C++ fixtures remain in the suite.

Host tests check indexed relocation batching, duplicate/type/bounds rejection,
atomic failure, file-loader ownership/partial reads/cancellation, and the older
malformed-ELF mutations. Sanitized allocator stress includes expanded-pool
exhaustion, large alignment, recovery and concurrent allocation.

The isolated source and logs are in `build/user-validation-large`. The working
tree kernel, native graphics changes and working browser are preserved. Package:
`build/dihos-user-large-20261029`, build `20261029T114017Z-8DCAFDCF4B14`.
All 87 manifest files/hashes are verified. With the VM off, update from Administrator
PowerShell in the repository:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-large-20261029
```

After boot:

```text
proc:run 0:/OS/System/Programs/UserRuntime/large.elf
proc:status
```

Allow roughly 30–60 seconds for this intentionally large test. The pointer,
terminal and window controls should remain usable while it loads. Expect
**exited**, status `0x56`, no live threads/reserved heap, and
`SACX large image: 20 MiB payload, relocations, BSS and 8 MiB allocation OK`.
Optional `large_fault.elf` intentionally faults on a write to its RO segment;
the desktop should survive. The reported Hyper-V run passed with exit `0x56`,
2,053 relative fixups, a 16.25 MiB heap peak and zero reserved/committed heap
afterward. The log alone does not measure responsiveness; native hardware
validation and input-latency measurements remain pending.
