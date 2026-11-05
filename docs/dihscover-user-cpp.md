# Freestanding C++ startup for the JavaScriptCore port

The isolated ARM64 runtime now loads a small **DihOS-targeted C++ executable**.
This is a prerequisite for JavaScriptCore; it is not JavaScriptCore itself or
a full libc/libc++/ICU implementation. The working NetSurf browser stays intact.

## What now works

`sdk/sacx/user_runtime/runtime.cpp` provides startup, initialization-array calls,
up to 32 registered C++ destructors, finalization, basic byte/string routines,
and `new`/`delete` backed by the checked SACX heap. The later
[arena allocator](dihscover-user-allocator.md) now suballocates many objects within
each owned region and supplies the C allocation functions. Initialization and destruction
execute in EL0, so ordinary preemption and fault isolation still apply. A stopped
or faulted task does not run arbitrary destructors in the kernel; its owned
resources are reclaimed by the existing supervisor.

The target is `aarch64-none-elf`, PIE, C++20, with exceptions, RTTI, stack-protector
dependencies and thread-safe local-static guards disabled. No Linux libraries
or undefined symbols are linked. Allocation failure terminates the app through
SACX rather than providing fake exception/unwind support. Each allocation still
used one bounded heap region in the original C++ package: at most eight live
allocations of up to 256 KiB. The newer arena package removes that object-count
limit; scalable VM and full target libraries remain required for JavaScriptCore.

The fixture exercises an initialization-array constructor, initialized object
and function pointers, virtual dispatch through an actual vtable, virtual delete,
overlapping `memmove`, heap allocation, yield and a registered destructor. The
disassembly retains the indirect vtable branch; this is not a constant-folded
substitute for virtual dispatch.

## Loader boundary

The loader retains its 256 KiB file/image limit and eight-load-segment limit.
It now accepts one bounded `PT_DYNAMIC` table for a **static PIE**, with up to
256 `R_AARCH64_RELATIVE` records and zero symbol indices. It accepts a strict
set of metadata tags emitted by the pinned build recipe. External imports,
interpreter/TLS segments, PLT binding, REL/RELR fixups, unknown dynamic tags,
executable relocation destinations, duplicate/unaligned destinations and
out-of-image/negative addends are rejected.

Dynamic records identify the fixup table even when section headers are absent.
Present nonempty relocation sections must agree with that table. All fixups are
validated before writing any output. Destinations must lie wholly in a loaded
non-executable segment, and values are the owned user-image base plus a bounded
addend. This permits vtables and initialization arrays to remain read-only after
loading: fixups happen through inactive kernel-owned backing before user entry.
The existing cache publication and page permissions are retained.

## Evidence

Host malformed-ELF tests cover actual C/C++ binaries, import/TLS/text/duplicate/
overflow rejection, stripped section headers, atomic fixup validation and 12,000
mutated inputs. Queued file-loader cancellation/error tests also pass. These are
focused robustness checks, not a complete executable-format security audit.

ARM64 QEMU passes in default and Hyper-V-style L0/L2/page-offset-GICR layouts,
including all earlier heap, isolation, timer/preemption and lifecycle tests.
The C++ task exits with 75 (`0x4B`), applies nine relative relocations, peaks at
4096 heap bytes, and leaves no owned physical allocations after exit.

```powershell
.\tools\build_user_programs.ps1
.\tools\test_user_runtime_qemu.ps1 -Python python -TimerPageOffset -FirmwareL2 -FirmwareL0
& 'C:\Program Files\LLVM\bin\llvm-objdump.exe' -p -R build/user-programs/cpp.elf
& 'C:\Program Files\LLVM\bin\llvm-nm.exe' --undefined-only build/user-programs/cpp.elf
```

`package_user_programs.py` includes the exact C++ sources/linker script/runtime
recipe and records hashes alongside `cpp.elf`. The isolated source snapshot
build excludes the unrelated native graphics work and retains the browser binary.

Prepared package: `build/dihos-user-cpp-20261028`, build
`20261028T171501Z-2AC5139E4D8B`. The isolated ARM64 kernel build, both QEMU
layouts, ELF/queued-loader tests, Hyper-V host tests and packaging checks passed.
All 62 manifest files/hashes were verified. Sources and validation logs are under
`build/user-validation-cpp`. The browser binary matches the previous working
package; the VHDX has not been modified.

With the VM off, update from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-cpp-20261028
```

Then boot and run:

```text
proc:run 0:/OS/System/Programs/UserRuntime/cpp.elf
proc:status
flush
```

Allow a few frames before status. Expected: **exited**, status `0x4B`, relative
relocations `0x9`, heap reserved/committed zero, peak `0x1000`, and
`SACX C++: startup, relocations, heap and destructors OK`.
The user confirmed this original C++ fixture on Hyper-V: six SACX calls, grants
`0x64`, nine relocations, exit `0x4B`, the expected success message, zero remaining
heap and peak `0x1000`. The newer arena-backed version has its own guest gate.

## Remaining JavaScriptCore work

The separate ARM64 Linux JSC build proves the pinned engine source can build;
this fixture proves initial DihOS C++ startup. Neither bridges the remaining gap.
The arena allocator is now implemented and passes host/QEMU tests. The next
target-runtime work needs further libc/C++ library primitives, followed by
owned threads/TLS/waits and the WTF platform
adapter. ICU data/runtime, larger incrementally loaded images and the other
measured dependencies remain required before the JSC language fixture can run
inside DihOS. DOM/WebCore and video remain later stages. Missing functionality
must fail explicitly rather than being replaced by success-returning stubs.
