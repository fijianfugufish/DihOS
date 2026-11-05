# Sparse browser VM adapter and JavaScriptCore

The SDK browser memory adapter now uses the sparse SACX service. Reservations
up to 4 GiB consume no payload pages until explicitly committed. Page-state
bookkeeping is allocated for touched 1 MiB spans rather than the entire virtual
range. Decommit returns physical backing; recommit zeroes it. Existing adapter
entry points, permission checks, guards and the physical process quota remain.
The kernel and userspace enforce different responsibilities: the SDK tracks
page state, while the kernel checks ownership, mappings, permissions and quota.

JavaScriptCore no longer sets the earlier 8 MiB structure-heap override. Its
upstream default for this platform is 4 GiB. The native SDK, ICU, RTTI, C/C++
support, bmalloc/WTF adapters and actual engine were rebuilt together. The
interpreter-only profile still disables concurrent GC; collection uses the
upstream synchronous collector, and kernel timer preemption remains active.
Executable allocations and JIT are still disabled.

## Validation

The [exact-binary execution record](browser-evaluation/native-javascriptcore-sparse-adapter-20261031.json)
passes all 39 ARM64 QEMU integration groups with a four-level firmware root
and 2 MiB firmware mappings. The engine's observed peak reservation was
`0x100801000` bytes, with `0x32d000` bytes (about 3.2 MiB) of committed heap
pages. This commitment excludes executable-image, stack and kernel metadata
storage; it is not a total process RSS measurement.

The full language/Unicode/Intl/promise/protected-root/GC/context-isolation
suite completed under 16 ms execution-frame pacing. A real infinite script
was preempted without engine syscalls, then cancelled with complete allocation
cleanup. Actual bmalloc/WTF and SDK fixtures covered GiB alignment, distant
pages, guard faults, decommit/recommit, protection and concurrent registry use.
The [kernel regression record](browser-evaluation/sparse-vm-adapter-l0-20261031.json)
also checks isolation, quota, 19 OOM rollback points and page-table reclamation.
Hyper-V host regressions and packaging checks passed. The subsequent
[user-reported Hyper-V run](browser-evaluation/native-javascriptcore-sparse-adapter-hyperv-20261031.json)
also completed: exit `0x57`, 416 slices, 10 timer preemptions, 406 SACX calls,
and zero remaining reserved/committed heap. Its peak committed heap was the
same `0x32d000` bytes. The transcript associates the package by conversation
context rather than a guest binary digest; exact-binary and peak-reservation
checks remain in the QEMU record. End-to-end UI latency, physical hardware
and this build's busy-script cancellation on Hyper-V remain unverified.

The prepared package is `build/dihos-user-vm-adapter-20261031`. It reuses the
exact kernel from the user's successful sparse-VM Hyper-V test; all 321 kernel
and include source files match that earlier isolated snapshot. Its kernel
SHA-256 is `4424b0c3e5d2ca33c3fdd5b675f30ee158bdbb297c617a961e0918fd69b55fa9`.
The validated NetSurf binary, resources and fonts are retained. User files
outside the system manifest are preserved by the update operation.

## Guest test

With the VM off, run from the repository in Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-vm-adapter-20261031
```

Then in DihOS:

```text
proc:run 0:/OS/System/Programs/UserRuntime/jsc.elf
proc:status
```

Admission reads about 49 MiB incrementally. Check status again until it reports
`exited`, exit status `0x57`, `SACX JavaScriptCore: native script evaluation OK`
and zero remaining reserved/committed heap. Optionally run `jsc_spin.elf`, use
the desktop while checking increasing preemptions, then cancel with
`proc:stop`. The SDK fixture `vm.elf` should exit with status `0x5a`.

## Reproduction

Use the native dependency/build sequence in
[the JSC port notes](browser-evaluation/jsc-dihos.md), rebuilding dependencies
against the current SDK. This run used the pinned WPEWebKit 2.54.2 and ICU 78.2
sources and a complete native `JavaScriptCore` library build. Exact compiler,
library, input and recipe hashes accompany the execution record and packaged
`JSC-SOURCES.zip`; no host engine is reused.

After linking both normal and `--spin` profiles, run from PowerShell:

```powershell
.\tools\test_user_runtime_qemu.ps1 -Python <python-executable> -WebkitSync -WebkitRandom -WebkitVm -Rtti -FeatureMain -Libm -JscSupport -Stdio -Jsc -JscSpin -JscFramePaced -FirmwareL0 -FirmwareL2 -ReusePrograms
python tools/record_jsc_native_test.py --sparse-adapter
python tools/record_sparse_vm_test.py --variant l0 --sparse-adapter
```

`-ReusePrograms` requires a matching verified base-program build stamp. Omit
it to rebuild base fixtures. For an isolated exported project, use
`tools/package_user_programs.py` followed by
`tools/package_jsc_probe.py <project> --sparse-adapter`, then the Hyper-V
packager. Verify the prepared package with:

```powershell
python tools/verify_user_package.py --package build/dihos-user-vm-adapter-20261031 --validation build/user-validation-vm-adapter-20261031 --qemu-log build/user-validation-vm-adapter-20261031/qemu-final.log
```

## Next browser milestone

This is a standalone engine test. Dihscover still uses NetSurf, so JavaScript
websites and YouTube playback are not enabled. Next implement the event-loop,
IPC and checked asynchronous resource services required by a native WebCore
view, then bring up local HTML/CSS/DOM/JavaScript fixtures and integrate input,
software painting and navigation. Media decoding and video presentation follow
working web applications. Audio output remains deferred.
