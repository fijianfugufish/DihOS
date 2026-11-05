# JavaScriptCore freestanding DihOS probe

This is the first target-source build inventory for the native SACX port of
JavaScriptCore. It does not replace Dihscover, update a VHDX, or run JavaScript
inside the guest. The earlier [ARM64 Linux probe](wpe-arm64.md) remains separate.

## Reproduce

Use ARM64 WSL with Python 3, CMake, Ninja, Clang/LLD 22, Ruby and AArch64
binutils. Prepare the pinned libc++ bundle with `tools/prepare_user_libcxx.py`.
The input WebKit archive is the existing
`build/browser-engine-arm64/wpewebkit-2.54.2.tar.xz`, SHA-256
`f3284e19e47fcb5ac8c63c2e1c113dbd65b2423eb8a545f5b345707829004f56`.

From the repository directory in WSL:

```sh
python3 tools/jsc_dihos_probe.py --inventory
```

The default cache is `~/.cache/dihos-jsc-target`. Source extraction, validation
and the initial copy can take time before the first progress line appears.
Logs and `report.json` go in `build/jsc-dihos`; preserve them before rerunning.
`--inventory` permits an incomplete engine build so that remaining translation
units are checked. Without that option, an incomplete engine returns failure.
Always inspect `engine_built` and `guest_validated`, rather than treating an
inventory command's zero exit status as an engine pass.

## Port boundary

The compiler targets `aarch64-none-elf`, with freestanding headers, the pinned
libc++ slice and SACX runtime. The script verifies that neither Linux nor Windows
target macros are present. Target include/library/package discovery is rooted
in an isolated sysroot; Linux ICU and the Linux runtime are not linked.

`tools/jsc_dihos/port-edits.json` records literal edits against the pinned source:
DihOS OS identification, selection of a native SACX thread dependency link
test, and real executable links for subsequent CMake capability checks. The
initial compiler ABI checks compile archives because there is no Linux CRT.
Later capability checks use a small target startup adapter and the real runtime
archive. No pthread capability result is forced true.

The thread check links an actual `std::thread` create/join fixture into a static
PIE using the existing user-program linker layout. Its undefined-symbol check
must be empty. This is link validation, not execution or evidence that WTF's
POSIX threading backend works on DihOS.

The target C99 math overlay supplies declarations needed by libc++ `<cmath>`.
It does not implement libm. Unsupported math calls must remain link failures;
the shipping NetSurf math helpers are not a suitable JavaScript math backend.

The embedding fixture in `sdk/sacx/apps/jsc_probe/main.cpp` is ready to evaluate
a small script through the real JavaScriptCore C API once the engine links.
Compiling that object alone is not JavaScript execution.

## Recorded result: 2026-10-29

With Ubuntu Clang 22.1.2, the target macro checks and all ten SACX runtime
object compilations passed. The native thread fixture linked without unresolved
symbols (ELF SHA-256
`bbbf22ed5eb08e1b8d7e4267188ad605256cc08e11bc870efb721c469cce1cb2`).
The embedding and upstream WTF allocator translation units compiled.

Full configuration stopped at missing target ICU headers and its data/i18n/uc
libraries (upstream requires at least 70.1). Six other source probes stopped at
missing ICU headers; the clock source stopped at missing `timespec` and
`CLOCK_REALTIME`. Those early errors do not exhaust the remaining port work.
The allocator also emitted a noreturn warning through the incomplete target
abort declaration, which needs correction before a warning-clean engine build.

Retained [machine-readable evidence](jsc-dihos-2.54.2.json) records source edits,
compiler, hashes, successful object imports and failed source diagnostics.
**Engine built: false. Guest validated: false.**

## Next acceptance gates

1. Validate the pinned ICU Unicode code/data at the final native executable
   link, with actual runtime dependencies resolved. The [target ICU archives](icu-dihos.md)
   now compile all 456 common/i18n sources and embed the genuine data. The
   [matching RTTI implementation](../dihscover-user-rtti.md) passes EL0 QEMU.
   Neither archive compilation nor staging constitutes an ICU guest test.
2. Complete linkage and execution of the native WTF thread, allocation, clock
   and related platform contracts. Core services and selected adapters now run
   in EL0; that does not establish whole-library coverage. Resolve remaining
   event-loop and C/library imports with real implementations. File operations
   remain denied until a scoped kernel file service exists.
3. Link the C-loop engine and embedding fixture with no unresolved imports,
   interpreter, or host dependencies. Inspect ELF segments, TLS, relocations,
   stack requirements and size against loader limits before deployment.
   The current full ICU data payload alone is 33,107,248 bytes, leaving only
   447,184 bytes under the 32 MiB file limit. Measure the final executable and
   deliberately budget its file/image/heap limits, or use a verified reduced
   data package; staging ICU does not establish that the final app fits.
4. Run script evaluation, cancellation and allocation failure inside EL0 while
   the desktop remains responsive. Check cleanup and repeated launches.

Only then move on to WebCore/DOM, native surfaces, resource loading and media.
JSC alone does not make YouTube or other web applications work.

After ICU staging, use the explicit dependency manifest:

```sh
python3 tools/jsc_dihos_probe.py --inventory --icu build/icu-dihos/stage-report.json
```

The source probes now pass the missing Unicode-header stage and expose native
thread primitives, register-context handling, floating-point environment and
clock requirements. The native backend must implement these contracts; defining
Linux/pthread platform flags or supplying fake signal-context headers is not a
port. The shipping browser and VHDX remain unchanged.

The completed retry **passes CMake configuration** with the target ICU archives.
Clang header maps are disabled using `USE_HEADER_MAPS=OFF`, because the release
archive omits the optional `hmaptool` helper; ordinary generated include paths
are used. The actual build stops in bmalloc's POSIX virtual-memory include
`sys/mman.h`. This requires a native allocator/backend contract, not a fake
POSIX availability result. The remaining source probes also expose the thread,
register-context, clock and floating-point environment contracts above.
The [retained follow-up evidence](jsc-dihos-icu-2.54.2.json) records configure
success and build failure separately. There is no runnable JSC guest binary yet.

The subsequent [native VM backend](../dihscover-user-vm.md) now replaces these
allocator memory calls with owned SACX services. Both the actual bmalloc header
functions and WTF OSAllocator implementation pass ARM64 EL0 tests. The build
advances to CryptoRandom's POSIX entropy-source fallback; it is not bypassed
with a weak random seed or fake file service.

The next [kernel-backed random implementation](../dihscover-user-random.md) now
replaces the native bmalloc/WTF random sources and passes EL0 tests. It collects
trusted firmware/RNDR seed material and uses BearSSL HMAC-DRBG; unavailable
entropy fails promptly. The build progresses to thread/register-context support.
The reported Hyper-V SDK test confirms firmware and RNDR availability for a
fresh boot; native hardware and snapshot/clone reseeding remain unvalidated.

The next [native WTF synchronization layer](../dihscover-webkit-sync.md) adapts
the pinned mutex, condition and thread-local key interfaces to SACX. Full WTF
thread creation/stack bounds and safe stopped-thread register inspection remain
separate dependencies. In particular, the register-context build failure is
retained until an actual kernel contract exists; no POSIX signal context is
fabricated. This work does not add JavaScript/DOM support to the shipping browser.

The following [native context and platform layer](../dihscover-user-context.md)
now implements process-local stopped-thread inspection, stack bounds, detach,
UTC clocks, floating-point environment, process identity and UTC calendar
conversion. Actual saved GPR/SIMD values, multiple stopped mutators, cancellation,
detached-worker cleanup and calendar vectors pass ARM64 EL0 integration tests.
The complete WTF Thread adapter cross-compiles; it has not yet run as a linked
WTF object. The script-only engine profile denies file operations because no
file grant or file-handle namespace is exposed to it yet.

Build-check correction: freestanding C++ `main` is mangled. The older weak
C-linkage entry bridge could let section garbage collection discard a C++
feature-check body, so those earlier configure results alone do not prove its
dependencies linked. The bridge now retains and invokes C, zero-argument C++
and argument-taking C++ entry points. Positive entry fixtures execute in EL0;
a negative C++ fixture with a missing dependency must fail the link. Previously
executed explicit `dihos_user_main` SDK tests are unaffected. Engine compilation,
final linkage and guest execution remain separate acceptance gates.

The [bounded full-build inventory](native-jsc-platform-build-20261029.json)
retains the 600-second build timeout and actual compile failures separately
from successful source probes. Integer arithmetic and WTF integer printing
compile without the SDK's unavailable standard iostream API. The native lock
timeout path uses resumable SACX sleep and monotonic deadlines, returning
success only after acquiring the lock.

The wider build exposes remaining native work in `PageBlock`, diagnostic text,
`RandomDevice`, process termination, main-thread setup, locale, CPU time and
memory footprint. Unix descriptor helpers cannot be assumed to exist in the
no-file profile. Four native adapters also needed `config.h` before WTF headers;
the probe now checks native sources using their actual CMake compiler flags so
extra probe defines cannot conceal that mistake.
The follow-up recorded in that inventory passes all seven native adapter
checks with actual CMake target flags. It explicitly skips the whole build;
the remaining full-build failures have not been declared fixed.

For a focused configure/source check using the existing generated target tree:

```sh
python3 tools/jsc_dihos_probe.py --inventory --icu build/icu-dihos/stage-report.json --build-timeout 0
```

Zero explicitly skips the whole engine build and keeps `engine_built=false`.
Omit the option for the default 600-second build, or supply a larger budget.
An inventory command's zero exit status does not mean the engine built.

The [native platform/resource layer](../dihscover-webkit-platform.md) now closes
the main-thread, locale, page-size, remaining random-device, diagnostic and
CPU/memory-reporting compile gaps. The entire native `libWTF.a` builds, and the
expanded ARM64 EL0 suite passes all 30 integration groups. This proves support
library compilation and the documented adapter tests, rather than whole-engine
execution. A missing `strcmp` exposed by the fixture link now has a real
unsigned-byte comparison implementation in the native runtime.

Use `--build-target WTF` to validate the support library, `--build-target
JavaScriptCore` for the engine archive and `--build-target jsc` for the standalone
shell. Archive builds retain `engine_built=false`; they do not establish a
runnable executable. `--reuse-build-dir` accepts only an existing same-source
directory directly inside the verified cache and always invokes CMake with
`--fresh`. This reruns real capability links while retaining unchanged object
files, avoiding stale feature answers after runtime changes. The native platform
has no dynamic-loader library; its CMake `CMAKE_DL_LIBS` is explicitly empty.

`tools/build_jsc_native_test.py` links the recorded embedding object against the
actual native engine/support/ICU/runtime archives. It verifies retained C API
references and dependency hashes, reports unresolved symbols and checks a linked
ELF with the production loader validator. It never deploys the result or treats
a successful link as guest execution.

For incremental header fixes, `--reuse-header-stage` may accompany
`--reuse-build-dir`. Both directories must belong to the verified cache and the
existing CMake configuration must reference that header stage. The probe checks
every staged header against the checkout, refreshes changed bytes and rejects
unexpected files or symlinks. The recorded content hash identifies the headers;
the retained directory name can reflect an earlier content hash. Stable include
paths let the compiler's dependency files select affected translation units.
The compiler command also records a hash of the header path set. Adding an
earlier include candidate can redirect an existing `#include` without appearing
in its old dependency file; a changed path set therefore invalidates every
target object. Content-only edits retain normal compiler dependency tracking.

The native JavaScriptCore archive now compiles in full. This is still separate
from linking and executing the embedding fixture. The freestanding `stdio.h`
now supplies the standard `BUFSIZ` constant required by `ConfigFile.cpp`.

`tools/build_user_libm.py` builds the 232 unmodified generic math sources from
[musl 1.2.5](https://musl.libc.org/releases/musl-1.2.5.tar.gz), pinned to SHA-256
`a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4`.
It verifies the entire extracted source tree and generates the upstream AArch64
type header. Compilation targets `aarch64-none-elf`; no musl system calls,
threads, files, allocator or loader are built. The report records remaining
floating-point dependencies and keeps guest validation false. The source and
copied copyright notice remain available for packaging. Pass
`--libm-report build/user-libm/report.json` to the embedding linker to include
that verified arithmetic archive. A successful archive build does not establish
math conformance or guest execution.

The complete native link also needs upstream's `JavaScriptCoreJIT` object
target: despite its name, it contains bytecode, parser and interpreter code used
by the no-JIT build. The probe now builds both targets and seals that object
target into `libJavaScriptCoreBytecode.a`. CMake's thin archives are converted
into self-contained archives, preserving distinct objects with identical base
names. Recorded archive hashes therefore cover the actual code, rather than
only paths to mutable build objects.

The [October 30 native library result](native-jsc-libraries-20261030.json)
records both engine components, WTF and bmalloc, and 32 passing ARM64 EL0
integration groups. It verifies the embedded test ELFs in the QEMU kernel.
The arithmetic test retains actual LLVM helper calls and checks 128 divisions
against independently generated Python big-integer results. It also exercises
the out-of-line libc++ hashing implementations through string-keyed unordered
maps and sets. C support tests exercise string operations, sorting, empty and
process-owned environment lookup, fixed C locale and LIFO exit callbacks.
The native signal registry accepts an empty handler set and rejects attempts to
install unsupported handlers. Thread stack queries use the owned native handle;
whole-WTF-thread lifecycle execution remains unvalidated.

Additional dependency/test commands, after the native probe completes:

```sh
python3 tools/prepare_user_builtins.py --fetch
python3 tools/build_user_libm.py --c-support --fixture-runtime-report build/jsc-dihos/report.json
python3 tools/build_user_jsc_support.py
python3 tools/build_webkit_sync_test.py
python3 tools/build_jsc_native_test.py --libm-report build/user-libm/report.json --support-report build/user-jsc-support/report.json
```

The first command fetches selected arithmetic sources at the existing LLVM
22.1.0 commit pin. The support builder verifies the corresponding libc++ source
blobs before compiling `hash.cpp` and `functional.cpp`. No Linux library or host
engine is used. The C support profile exposes no inherited host environment;
its locale remains fixed to C, independently of the explicit ICU browser locale.

On Windows, the expanded integration command is:

```powershell
.\tools\test_user_runtime_qemu.ps1 -Python python -WebkitSync -WebkitRandom -WebkitVm -Rtti -FeatureMain -Libm -JscSupport
```

That initial result recorded 21 unresolved C-library symbols. The
[native C runtime](../dihscover-native-c-runtime.md) now supplies real formatting,
numeric parsing, thread-local error storage and closed-profile streams; its
[shared-SDK execution result](native-c-runtime-20261030.json) passes 33 ARM64
integration groups. Header-path cache invalidation was added after the native
FILE interface exposed a stale constructor in an incremental archive. Engine
objects are rebuilt consistently before retrying the embedding link.
The shipping browser and VHDX have not been updated. Component execution does
not establish JavaScript evaluation or web-page compatibility.

## Native execution gate, 2026-10-30

The [native execution result](native-javascriptcore-20261030.json) now records
34 passing ARM64 EL0 integration groups, including the real interpreter-only
JavaScriptCore. It creates a global context, evaluates a JavaScript loop to
4950, releases the context and exits with status 87 (0x57). All process-owned
pages are reclaimed. The evidence recorder compares the exact engine ELF
against the bytes embedded in the tested kernel; this is not the Linux engine.

The executable is approximately 49 MiB with full ICU data. The production
loader's file cap is now 64 MiB; its 64 MiB mapped-image cap and 32,768 relocation
cap remain enforced. The linker explicitly places the mutable WebKit config
and opcode tables in writable, non-executable data and verifies their mapping.

The startup profile uses upstream options customization with an 8 MiB aligned
structure heap. The desktop default attempts 4 GiB down to 32 MiB; all of those
exceed the current native region limit. No allocator assertion is disabled.
This bring-up profile is not a substitute for sparse virtual reservations and
larger browser memory budgets. See the [native C runtime commands](../dihscover-native-c-runtime.md).

Run the engine gate after building the verified target dependencies and linker:

```powershell
.\tools\test_user_runtime_qemu.ps1 -Python python -WebkitSync -WebkitRandom -WebkitVm -Rtti -FeatureMain -Libm -JscSupport -Stdio -Jsc
python tools/record_jsc_native_test.py
```

Hyper-V and physical-hardware execution of this engine are separate gates.
WebCore/DOM, browser event-loop integration, page rendering and media playback
are not implemented by this script test. The existing NetSurf browser remains
the shipping baseline.
