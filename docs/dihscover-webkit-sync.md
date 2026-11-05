# Native WebKit synchronization

The experimental WebKit 2.54.2 port uses SACX mutexes, conditions and
generation-tagged thread-local keys through the actual WTF `Mutex` and
`ThreadCondition` interfaces. It does not enable `USE(PTHREADS)` or expose
kernel pointers. The shipping NetSurf application is unchanged.

`tools/jsc_dihos/port-edits.json` records exact, single-match changes to the
pinned upstream header and build selection. `ThreadingPrimitivesDihOS.cpp`
provides the methods; the existing native synchronization/key runtime handles
ownership checks, bounded kernel waits and destructor cleanup. Exhaustion or
invalid use terminates the application rather than spinning in kernel code.

Timed waits reject NaN, negative infinity and past deadlines, preserve positive
infinity as an untimed wait, round positive fractional milliseconds upward and
saturate very large intervals before integer conversion. A wall-clock deadline
is converted to a monotonic deadline at entry; subsequent wall-clock adjustments
do not alter that wait. Production `WallTime::now()` is still a required native
clock dependency, not replaced with uptime.

## Reproduce

Run in ARM64 Ubuntu WSL from the checkout:

```sh
python3 tools/jsc_dihos_probe.py --inventory --icu build/icu-dihos/stage-report.json
python3 tools/build_webkit_sync_test.py
```

Then from Windows PowerShell:

```powershell
.\tools\test_user_runtime_qemu.ps1 -Python 'C:\Users\joshu\AppData\Local\Programs\Python\Python312\python.exe' -WebkitSync -WebkitRandom -WebkitVm -Rtti
```

The adapter fixture links the actual patched WTF header, native synchronization
implementation and production `CurrentTimeDihOS.cpp` with no unresolved imports.
The QEMU kernel supplies a documented test-only RTC anchor to the production UTC
implementation. There is no replacement wall-clock function in the app fixture.
Tests cover missing UTC, clock variants, try-lock contention, timeout with
ownership restored, finite/infinite and very large deadlines, signal/broadcast,
three simultaneous workers, six joins, key generations and exit destructors.
The expanded fixture also checks that main and worker threads share the actual
native process identity. It executes the pinned `FileHandle` class and native
no-file adapter: default and foreign/adopted handles remain invalid; reads,
writes (including empty requests), seek, size, ID, truncate, flush and mapping
fail; destination bytes stay unchanged. Explicit empty-file views remain valid
zero-length values, distinct from failed mapping.
Invalid unlock exits with 187; a stale key faults inside its own process.
The successful fixture exits with 95 (`0x5f`), with four repeated cleanups.

The earlier [synchronization-only result](browser-evaluation/native-webkit-sync-20261029.json)
used a fixture-only clock. The subsequent [native context/platform layer](dihscover-user-context.md)
adds the production clocks, real stopped-thread inspection, guarded 1 MiB stacks
and detached-worker cleanup. Its combined ARM64 EL0 suite passes; Hyper-V
validation of these new operations remains pending.

## Remaining engine contracts

`ThreadingDihOS.cpp` now implements the WTF thread-object hooks with SACX
creation/join/detach, native TLS keys, stack bounds and stopped-thread snapshots.
That object adapter is cross-compiled; its complete runtime test still needs a
linked WTF/JSC library. The core SACX contracts and synchronization/clock methods
are tested separately in EL0.

The rest of WTF, remaining C/runtime imports and a complete JSC link still
precede guest JavaScript execution. DOM/WebCore, browser resource loading and
rendering precede JavaScript websites; media decoding follows for muted video.
