# Owned ARM64 userspace memory through SACX

This adds the first heap/VM service to the isolated development ELF runtime.
It is a browser-runtime prerequisite; NetSurf remains on its existing path and
WebKit does not yet run in DihOS.

Apps invoke SVC through their local SACX table. `mem_alloc` and `mem_free` now
have userspace wrappers, alongside the existing clock/log/yield/exit members.
The kernel accepts no physical addresses, page-table pointers or kernel function
pointers from the app. Memory calls run with the app paused and the kernel
translation root restored. Capability checks precede the memory operation.

## App-facing contract

The ABI envelope/version remain unchanged. Operation numbers append to the
existing ABI; QUERY advertises operation bits `0x3ff`. The new memory capability
is bit 6 (`0x40`). Development ELFs receive clock/log/memory grants `0x64`.
This is a loader policy for development fixtures, not configurable app manifests.

| Operation | Number | Arguments | Result |
|---|---|---|---|
| Reserve | 6 | Page-aligned byte length, zero, zero | Owned page-aligned user address or negative error |
| Commit | 7 | Address, page-aligned length, READ or READ\|WRITE | Zero after zero-initializing and exposing previously uncommitted pages |
| Protect | 8 | Address, page-aligned length, NONE/READ/READ\|WRITE | Zero; preserves data and commitment |
| Release | 9 | Exact reservation base, zero, zero | Zero after revoking access and freeing the whole region |

Pages are 4096 bytes. A task has at most eight regions, each at most 256 KiB.
Commit/protect calls affect at most 64 KiB. READ is 1, WRITE is 2; executable
permissions are rejected. Partial release and decommit are not implemented.
Commit rejects any overlap with already committed pages rather than clearing
live data. Protect cannot expose uncommitted pages. A NONE-protected committed
page can be restored to READ/RW without losing its contents.

**Reservations initially allocate physical backing eagerly.** They deny EL0
access until commit; they are not sparse/demand-paged reservations. Each owns
two additional inaccessible guard pages. The kernel charges these guards too.
The development task's 4 MiB limit first reserves its image/stack/guards and the
maximum private-table allowance; the remainder is the heap's physical quota.
The region limits independently cap payload reservations at 2 MiB. These limits,
contiguous backing and lack of aligned large/sparse regions are bring-up limits;
the 18 MiB Linux JSC probe is still outside this loader/runtime's capabilities.

`mem_alloc(size, &pointer)` rounds up to pages, reserves and commits in bounded
chunks, rolling back its reservation on error. It supports at most 256 KiB per
allocation and returns zeroed memory. `mem_free(pointer)` releases that exact
allocation. There is no promise to detect stale pointers when an address is
later reused within the same process; ordinary C lifetime rules still apply.

## Isolation and lifecycle

The manager matches every range against that task's kernel-owned region table.
It rejects foreign/interior/double releases, unaligned/wrapping/out-of-region
ranges, unknown flags, exhausted quotas and exhausted region slots. The private
mapper verifies exact physical backing and prepares all L3 leaves before access
changes. Page-table allocation failure rolls back newly allocated tables and
parent descriptors before backing is freed.

User pages remain non-global and non-executable. Permission changes are
published before re-entry, which retains the existing full TLB invalidation on
root switches. Release revokes EL0 access before backing returns to the allocator.
Exit, fault and cancellation discard the inactive private VM and free every heap
reservation, including uncommitted backing. `proc:status` retains heap peak,
reports current reserved/committed bytes, and now prints normal exit status.

## Validation

The ARM64 QEMU suite executes production memory, scheduler, page-table, timer
and exception code. It covers zeroing, read-only/no-access transitions, hardware
NX faults, a real EL0 read-only write fault, foreign/interior/double releases,
guard pages, quota/slot exhaustion, the full 64-page commitment bitmap, bounded
permission work, overflow rejection and transactional allocation failure.
It also exercises repeated zeroed reuse and exit/fault/cancel cleanup with no
remaining physical allocations. The existing preemption/ELF/SACX tests remain.
Host service tests check that missing memory grants reject each operation.

```powershell
.\tools\test_user_runtime_qemu.ps1 -Python python -TimerPageOffset -FirmwareL2 -FirmwareL0
```

The user confirmed the Hyper-V memory fixture: 52 SACX calls, grants `0x64`,
status `0x4A`, the expected success message, zero reserved/committed bytes and
peak `0x4000`. The deliberate read-only fault fixture and native hardware test
remain unconfirmed. The commands below retain the original package recipe.

Prepared package: `build/dihos-sacx-memory-20261028`, build
`20261028T165502Z-AAE9F5921AC4`. The isolated ARM64 kernel build, both ARM64
QEMU layouts, Hyper-V host tests and packaging checks passed. All 61 manifest
files/hashes were verified, with the working browser binary retained. Source
hashes and host results are under `build/user-validation-memory`.
The VHDX has not been changed by this packaging step.

With the VM off, update from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-sacx-memory-20261028
```

Then boot and run:

```text
proc:run 0:/OS/System/Programs/UserRuntime/memory.elf
proc:status
proc:run 0:/OS/System/Programs/UserRuntime/memory_fault.elf
proc:status
proc:stop
flush
```

Allow a few desktop frames before requesting status. `memory.elf` should exit
with status `0x4A` and log `SACX heap: zeroing, protection, reuse and cleanup OK`.
Reserved/committed bytes should be zero and peak at least `0x4000`. The second
program should report a contained **faulted task** with peak `0x4000`, while the
desktop continues. This deliberate task fault must not produce an OS crash.

The next [freestanding C++ startup/relative-relocation step](dihscover-user-cpp.md)
passes QEMU; its guest check remains pending. Further runtime work: target
C/C++/ICU library requirements, scalable virtual
regions, threads/TLS/waits, IPC/software surfaces, then the WebCore platform view.
General app policy and scoped file/network handles remain separate follow-ups.
