# Native JavaScriptCore development test

On 2026-10-30 the real ARM64, interpreter-only engine passed inside the DihOS
EL0 runtime in QEMU. It creates a context, evaluates a JavaScript loop to 4950,
releases its context and exits with status 87 (0x57). All owned process memory
is reclaimed. The [execution record](browser-evaluation/native-javascriptcore-20261030.json)
includes all 34 integration groups and verifies that the exact engine ELF was
embedded in the passing test kernel.

This is a standalone engine test. NetSurf does not gain JavaScript/DOM support
from it. WebCore integration and media playback remain later work. The
[user-reported Hyper-V run](browser-evaluation/native-javascriptcore-hyperv-20261030.json)
also passed: exit 0x57, expected success message and zero remaining heap
reservations/commitments. This fixture created no engine threads and recorded
no timer preemptions. Physical-hardware execution remains unverified.

## Prepared Hyper-V package

`build/dihos-user-jsc-20261030`, build `20261030T214924Z-5AACAC19745F`, contains
an isolated kernel build from the published browser baseline plus the native
runtime changes. The previously working browser is retained byte-for-byte.
The standalone test and its source/patch archive are under
`OS/System/Programs/UserRuntime`. Asset hashes, host Hyper-V regressions and
packaging tests passed; see `build/user-validation-jsc-20261030/result.json`.
The normal checkout's kernel binary was preserved.

The attempted VHD update was denied by Hyper-V authorization before mounting.
With the VM off, run from the repository in **Administrator PowerShell**:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-jsc-20261030
```

After a fresh boot, run in DihOS:

```text
proc:run 0:/OS/System/Programs/UserRuntime/jsc.elf
proc:status
```

Admission reads about 49 MiB incrementally. While it is loading, use the
desktop and check `proc:status` again. Expected completion: `exited`, exit
status `0x57`, and `SACX JavaScriptCore: native script evaluation OK`.
`proc:stop` cancels pending loading or execution.

## Reproduction and current limits

The [engine build notes](browser-evaluation/jsc-dihos.md) and
[C runtime commands](dihscover-native-c-runtime.md) describe the pinned native
dependencies, target builds and `-Jsc` QEMU command. The embedding linker
compiles the fixture using the verified target flags, requires actual JSC C API
imports, checks zero unresolved final imports and applies the production ELF
validator. The file cap is 64 MiB; the mapped-image and relocation caps remain
64 MiB and 32,768 respectively.

The 8 MiB aligned structure-heap profile uses upstream options customization
inside its required unfinalized-access scope. The native allocator currently
charges backing at reservation time; its 16 MiB region cap includes alignment
padding. Sparse virtual reservations and a larger browser memory budget are
required before treating this profile as suitable for modern web workloads.
Mutable WebKit configuration and opcode tables are explicitly mapped RW/NX;
the linker verifies that placement.

## Extended suite and busy-engine cancellation

The [extended QEMU result](browser-evaluation/native-javascriptcore-suite-20261030.json)
passes 35 integration groups. The real engine checks modern syntax/classes,
closures, Map/Set, JSON, BigInt, typed arrays/DataView, Unicode regular
expressions, normalization/Intl, native callbacks, exceptions and promise
microtask checkpoints. Protected values survive four synchronous full-GC
cycles with allocation pressure; a fresh context has isolated globals.

A second executable evaluates an infinite loop. The kernel regains control
for at least 24 timer preemptions after evaluation starts; the final eight
are explicitly checked to contain no engine syscalls. Cancelling the process
reclaims all owned allocations. This establishes kernel progress in QEMU;
Hyper-V pointer/desktop responsiveness still needs the guest check below.

The new package is `build/dihos-user-jsc-suite-20261030`, build
`20261030T220336Z-5AACAC19745F`. Its kernel and existing browser are identical
to the preceding package; the two JSC test executables and source bundle are
updated. From Administrator PowerShell with the VM off:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-jsc-suite-20261030
```

After booting, run `proc:run 0:/OS/System/Programs/UserRuntime/jsc.elf` and then
`proc:status`. Expected completion remains exit `0x57` and the success message;
this now follows all the extended assertions. Then run:

```text
proc:run 0:/OS/System/Programs/UserRuntime/jsc_spin.elf
proc:status
proc:stop
```

While the busy test runs, move windows/use the desktop and check increasing
preemptions in `proc:status`. It intentionally never exits on its own. Stop it
before running other process tests. These checks do not add DOM/browser APIs
or media playback to the shipping browser.

Build both test profiles against the verified native engine libraries in WSL:

```sh
python3 tools/build_jsc_native_test.py --libm-report build/user-libm/report.json --support-report build/user-jsc-support/report.json --stdio-report build/user-stdio/report.json
python3 tools/build_jsc_native_test.py --spin --libm-report build/user-libm/report.json --support-report build/user-jsc-support/report.json --stdio-report build/user-stdio/report.json
```

On Windows, add `-JscSpin` to the documented `-Jsc` QEMU command, then record
with `python tools/record_jsc_native_test.py --extended`. Stage with
`python tools/package_jsc_probe.py <isolated-project> --extended` before
creating a fresh Hyper-V package. The original smoke-test evidence is retained.

## Desktop-paced GC correction

The [expanded Hyper-V transcript](browser-evaluation/native-javascriptcore-suite-hyperv-20261030.json)
showed a successful busy-loop preemption/stop test, but the main suite was
still running at `allocation batch OK`. It was not a completed suite pass.
Pacing the real engine in QEMU reproduced prolonged collection with repeated
clock syscalls. Upstream concurrent marking computes deadlines from wall-clock
progress; our coarse syscall/scheduler delivery can consume those deadlines
before useful marking work proceeds.

The current bring-up profile selects upstream `useConcurrentGC=false`. Full
collection remains enabled and protected-root tests still force synchronous
full GC. JavaScript pauses during collection; the kernel can still preempt
the entire process. Concurrent GC is deferred until fast clock/event delivery
and appropriate scheduling are implemented and tested, rather than treating
the earlier fast-polling test as sufficient evidence.

The desktop now uses a bounded runtime service instead of returning to the UI
after every short syscall. It handles at most 64 steps with a 1 ms execution
budget, preserving process/thread round-robin order. It returns immediately
after timer preemption, explicit yield, nonempty log, admission work or exit,
and never requests less than the timer's supported 100 microseconds. Kernel
service/cleanup time can exceed the budget; this is not an end-to-end latency
guarantee. The original single-step runtime API remains available.

The [desktop-paced execution record](browser-evaluation/native-javascriptcore-desktop-20261030.json)
passes 37 ARM64 integration groups. The actual language/GC suite completes
with 16 ms execution frames, alongside busy-engine cancellation, batching,
sleep/yield/log boundaries, CPU-task fairness and idle cleanup. Admission is
tested incrementally but is not frame-paced in this check. All 13 host
Hyper-V regression groups also passed. The
[updated Hyper-V transcript](browser-evaluation/native-javascriptcore-desktop-hyperv-20261030.json)
now reports successful suite completion: exit 0x57, expected success log,
seven timer preemptions, 406 syscalls and zero remaining heap reservations or
commitments. Physical-hardware execution remains unverified.

The update package is `build/dihos-user-jsc-desktop-20261030`. With the VM off,
run from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-jsc-desktop-20261030
```

Repeat `proc:run 0:/OS/System/Programs/UserRuntime/jsc.elf` and check for
`exited`, status `0x57` and the success message before starting another test.
Then repeat the busy-loop/`proc:stop` check above while using the desktop.

For reproduction, use the existing `-Jsc -JscSpin` QEMU command with
`-JscFramePaced`, record with `tools/record_jsc_native_test.py --desktop`, and
stage the matching newly built isolated kernel project with
`tools/package_jsc_probe.py <isolated-project> --desktop`. Historical package
and execution evidence is retained separately.

The [sparse browser adapter milestone](dihscover-user-sparse-adapter.md) now
rebuilds and executes the real engine with its upstream 4 GiB structure-heap
reservation instead of the earlier 8 MiB override. Its separate 2026-10-31
record passes 39 integration groups and retains the earlier evidence above.
