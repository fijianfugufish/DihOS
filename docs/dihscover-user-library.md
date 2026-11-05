# First libc++ library slice for DihOS userspace

This milestone links upstream C++ containers and strings into isolated ARM64
DihOS executables. It extends the target runtime for JavaScriptCore; it does not
run JavaScriptCore or WebCore in the guest.

## Pinned input and build boundary

The input is [LLVM libc++ 22.1.0](https://github.com/llvm/llvm-project/tree/llvmorg-22.1.0/libcxx),
commit `4434dabb69916856b824f68a64b029c67175e532`, matching the installed Clang
22.1.0 source revision. The canonical bundle contains 1,681 headers/source/licence
files, with tree SHA-256:

```text
b4bbb0028e6c9f5062949cdc739f663d0409242d724cd3e8fe742f22bffef0b8
```

`tools/user-libraries.lock.json` pins the commit and tree digest.
`prepare_user_libcxx.py --fetch` obtains a sparse checkout, verifies its commit,
checks source bytes against Git blob IDs (normalizing checkout CRLF), and exports
a portable source bundle. Regular builds verify all bundle files and reject
modified, missing or extra headers and linked entries. They do not fetch silently.
The integrity tests use separate fixtures and leave the real bundle untouched.

CMake configures only libc++'s header/site options. The selected upstream
`algorithm.cpp` and `string.cpp` are compiled with `aarch64-none-elf`, C++23,
`-nostdinc++`, exceptions/RTTI disabled, and explicit target include paths into
`libdihos-libcxx.a`. The fixtures link it statically with dead-code elimination.
This is a **selected library slice**, not a completed upstream libc++.a build.
The later [synchronization adapter](dihscover-user-sync.md) uses a separate site
profile for standard mutexes/once and native condition waits.
Unused functions may require unported C APIs or compiler builtins; there are no
fake definitions to make those dependencies appear supported. The final test
executables have no interpreter or undefined symbols.

The original site configuration disables threads, filesystem, localization, random-device,
wide-character and timezone services. Parallel algorithms use the serial backend.
Clang supplies the fundamental C types, including `max_align_t`. A local
`mbstate_t` ABI type allows headers to parse without claiming multibyte conversion
functions. Existing C declarations alone do not provide an implementation: an
unsupported call still fails to link. The newly needed `memchr` is implemented
locally and exercised by byte-string searches, including embedded NULs.

## Working behavior and failure isolation

The positive fixture exercises:

- growing, sorting, copying and clearing a 1,024-element `std::vector`;
- `std::span` access and `std::optional` value/reset behavior;
- `std::unique_ptr` ownership and `std::array` allocation;
- `std::string` growth, copying, replacement, comparison, substring/search,
  empty searches and embedded-NUL byte handling.

These use the SACX-owned TLSF arena. All library execution remains in EL0.
The existing image/heap size limits and single-thread restriction still apply.
Byte-string correctness is not a Unicode shaping, locale or ICU implementation.

Libc++ extensive hardening is enabled with enforce semantics. Its assertion
handler and verbose-abort hook terminate the current app through SACX, retain a
bounded reason string and let the kernel reclaim its resources. They do not use
host I/O or kernel function pointers. The negative fixture deliberately indexes
beyond a vector; it exits with `151` (`0x97`) while the kernel continues.

## Reproduction and guest gate

First-time dependency setup:

```powershell
python tools/prepare_user_libcxx.py --fetch
```

Then build and test from the repository root:

```powershell
.\tools\build_user_programs.ps1 -Python python
.\tools\test_user_runtime_qemu.ps1 -Python python -TimerPageOffset -FirmwareL2 -FirmwareL0
python tools/test_user_libraries.py
```

The user confirmed both library fixtures on Hyper-V: the positive test exited
`0x4D` with the expected message; the bounds fixture exited `0x97` with the correct
assertion while terminal commands continued. Both reclaimed a 256 KiB peak heap.
The positive and negative library fixtures also pass in the default and Hyper-V-style
ARM64 QEMU layouts, alongside all earlier process, preemption, heap, arena, C++
and relocation tests. Both leave no owned physical allocations. Source integrity
and malformed-ELF tests also pass. The new [native thread/TLS primitives](dihscover-user-threads.md)
have their own guest gate; libc++ thread support remains disabled.

Prepared package: `build/dihos-user-library-20261028`, build
`20261028T180216Z-1DD065149788`. The isolated ARM64 build, both QEMU layouts,
source-integrity, malformed-ELF, Hyper-V host and deployment checks passed.
All 68 manifest files/hashes were verified. Exact sources and validation logs
are under `build/user-validation-library`; the VHDX has not been modified.

With the VM off, update from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-library-20261028
```

Then boot and run one at a time:

```text
proc:run 0:/OS/System/Programs/UserRuntime/library.elf
proc:status
proc:run 0:/OS/System/Programs/UserRuntime/library_fail.elf
proc:status
flush
```

Allow a few frames before each status. The first should be **exited**, status
`0x4D`, with `SACX libc++: containers, strings, algorithms and ownership OK`.
The second should be **exited**, status `0x97`, with a `libc++:` assertion reason.
That is an intentional app abort, not a kernel crash. Reserved/committed memory
should return to zero in both cases.

The isolated source snapshot carries the pinned dependency bundle for rebuilding.
The guest package carries LLVM's complete libc++ licence and `LIBRARIES.JSON`;
it retains the working browser. Further C/ICU support, larger images/heaps,
the standard-library/WTF thread adapters and further runtime support remain before JavaScriptCore can
run the language fixture inside DihOS.
