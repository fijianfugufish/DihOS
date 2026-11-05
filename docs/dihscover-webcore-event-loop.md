# Native WebCore event-loop foundation

The next browser platform service now runs inside an ARM64 EL0 process:
WebKit's actual generic WTF RunLoop, its native timers and a bounded DihOS
dispatch queue. This is a scheduling foundation for WebCore, not a DOM or
browser implementation. NetSurf remains the shipping Dihscover engine.

## Service contract

`tools/webcore_dihos/DispatchQueue` accepts up to 32 pending functions. It
processes at most eight entries per run-loop turn, preserving FIFO order and
returning backpressure immediately when full. Cancelled entries still occupy
their slots until disposal. IDs are never reused within a queue; cancellation
of completed, executing or already-cancelled work returns false. Cancellation
does not interrupt a running callback.

Posting and pending-work cancellation are thread-safe. Accepted functions and
their captures execute or are destroyed on the owning run loop; rejected
functions retain caller ownership. Closing is owner-thread-only, rejects
further posts and disposes pending captures outside the queue lock. Callbacks
also run outside the lock. A retained queue reference protects scheduled
drains. Callers must close a queue before releasing their last reference.

These are userspace operations over the existing SACX thread/wait boundary.
They add no privileged pointers or kernel callbacks. The work-item budget is
not a time guarantee: one callback may run indefinitely, and kernel timer
preemption remains necessary. Process IPC and checked resource services are
still separate work.

## Native lifetime correction

The initial fixture exposed a shutdown assertion after its callback tests
completed. Its first build also used debug header layouts with Release
libraries; the concrete-object fixture now matches the engine's `NDEBUG`
and extensive libc++ hardening profile.

The remaining failure came from the pinned `RedBlackTree::remove`: a removed
node retained checked child links. An inactive timer could therefore retain
a checked link to another timer's scheduled task and abort when that task was
destroyed first. The experiment's explicit, hash-checked patch calls `reset()`
on the removed node after tree fixup. Checked-pointer lifetime assertions stay
enabled. Timer shutdown now passes without leaking objects or changing their
destruction order to evade the check.

Only the experiment compiles a replacement generic RunLoop object with that
header overlay. Sealed baseline libraries and the previously tested JSC
executable remain intact. The patch belongs in the eventual native WebCore/WTF
build configuration; this experiment does not modify upstream caches in place.

## Validation and platform audit

The [exact-binary record](browser-evaluation/native-webcore-eventloop-20261031.json)
passes all **40 ARM64 QEMU integration groups**. The new fixture covers bounded
FIFO admission, cancellation, stale handles, turn fairness, 128 cross-thread
deliveries, cross-thread cancellation, capture disposal, one-shot/repeating/
stopped timers, idle condition waits, re-entering the run loop, 100 queue-close
cycles, and a protected JavaScript callback scheduled by a real native timer.
Its promise microtask completes in the expected order. The final process exits
with status `0x67`; the harness verifies complete process-page cleanup.
Execution uses bounded desktop service with 16 ms frames. This does not measure
end-to-end input latency.

`tools/webcore_dihos/audit.py` checks the pinned WPEWebKit 2.54.2 archive and
records seven relevant platform sources plus 47 CMake dependency declarations
in `build/webcore-dihos/platform-audit.json`. Declarations include conditional
features; this is not a resolved dependency graph or successful WebCore build.
The stock WPE configuration includes Linux-facing GLib/GioUnix, Soup3 and Epoxy
dependencies. DihOS needs its own native WebCore platform configuration and
resource/graphics adapters rather than claiming those host services exist.

## Hyper-V package and test

The verified package is `build/dihos-webcore-loop-20261031`, build
`20261031T112516Z-4424B0C3E5D2`. It preserves 111 baseline files byte-for-byte,
including the kernel, browser, fonts, prior fixtures and engine source archive.
The build identity is regenerated and four event-loop artifacts are added.
The subsequent [user-reported Hyper-V run](browser-evaluation/native-webcore-eventloop-hyperv-20261031.json)
completed with exit `0x67`, 323 slices, three timer preemptions and 320 SACX
calls. Both created worker threads exited and joined; remaining reserved and
committed heap were zero, with `0x16c000` peak committed heap bytes. The guest
transcript associates the package by conversation context rather than an ELF
digest. Complete process-page cleanup is established separately in QEMU;
physical hardware and end-to-end UI latency remain unverified.

With the VM off, from Administrator PowerShell at the repository:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-webcore-loop-20261031
```

After booting:

```text
proc:run 0:/OS/System/Programs/UserRuntime/eventloop.elf
proc:status
```

Admission reads approximately 49 MiB. Check again until the task reports
`exited`, status `0x67`, `SACX WebCore event loop: dispatch, timers, cancellation
and JavaScript callbacks OK`, and zero remaining heap reservations/commitments.
The existing `jsc.elf` test remains available. No audio output work is included.

## Reproduction

After building the verified native JSC dependencies, use ARM64 WSL:

```sh
python3 tools/webcore_dihos/audit.py
python3 tools/build_jsc_native_test.py --event-loop --libm-report build/user-libm/report.json --support-report build/user-jsc-support/report.json --stdio-report build/user-stdio/report.json
```

Then PowerShell:

```powershell
.\tools\test_user_runtime_qemu.ps1 -Python <python-executable> -WebkitSync -WebkitRandom -WebkitVm -Rtti -FeatureMain -Libm -JscSupport -Stdio -Jsc -JscSpin -JscFramePaced -WebcoreLoop -FirmwareL0 -FirmwareL2 -ReusePrograms
python tools/webcore_dihos/record_test.py
python tools/webcore_dihos/package_test.py
.\tools\hyperv.ps1 -Action Package -ProjectRoot .\build\webcore-loop-validation-20261031\source -AssetSource .\build\hyperv-assets -PackagePath .\build\dihos-webcore-loop-20261031 -IncludeDihscover
python tools/webcore_dihos/package_test.py --verify
```

Staging refuses an existing source tree; choose another `--validation` path
for a fresh staging run. `-ReusePrograms` requires an unchanged verified base
fixture stamp. The event-loop executable, source overlay, build report and
exact-binary test record accompany the package. `WEBCORE-LOOP-SOURCES.zip`
overlays the retained `JSC-SOURCES.zip`, which supplies matching SDK, pinned
engine/dependency sources and licences.

## Following implementation gates

1. Add checked asynchronous resource requests and an owned process-message
   transport, using this queue for owner-thread completion delivery. Preserve
   USB/NetVSC transport ownership, cancellation and bounded buffering.
2. Create the native WebCore feature/platform build configuration and port
   dependency interfaces without advertising unsupported Linux capabilities.
3. Supply font/image/software-graphics backends and Frame/Page clients. Execute
   local HTML/CSS/DOM/JavaScript fixtures before connecting a native view to
   Dihscover's toolbar, input and surfaces.
4. Validate web apps, then decoded muted video. JavaScript website support and
   YouTube playback remain unimplemented by the event-loop milestone.
