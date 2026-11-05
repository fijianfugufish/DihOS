# Native C support for ARM64 EL0 engine programs

The native runtime uses a thread-local error value, exposed by
`__errno_location()`. Its `errno.h` and opaque `_IO_FILE` declarations live in
`sdk/sacx/user_runtime/libcxx/c`. Search that directory after libc++ wrappers and
before the freestanding C headers. Legacy SACX applications keep their existing
C shim and `dihs_file` interface.

`tools/build_user_libm.py --c-support` builds pinned musl 1.2.5 math and selected
string/sorting routines. Environment lookup starts with an empty process-owned
environment. C exit callbacks share the native C++ finalization registry. Locale
selection is fixed to C; unsupported locale names fail.

`tools/build_user_stdio.py` compiles musl's actual formatting and numeric-scanning
algorithms. Its private FILE-header adaptation removes an unused syscall-header
include and preserves C linkage for the C++ backend. Source archives and source
bytes are verified before compilation. No musl syscall implementation is built.

The script-process stream policy is explicit:

- Standard input returns EOF.
- Standard output/error send diagnostic text through the bounded SACX log call.
  NUL characters are ignored by this text device; subsequent bytes are retained.
- Streams are unbuffered and use native recursive locks. Full and line-buffering
  requests fail with `EINVAL`.
- File opens fail with `EACCES`. The profile has no descriptor or path access.
- Seeking fails with `ESPIPE`; closing and error/EOF state changes are real.
- Wide-character C stdio uses the fixed ASCII C locale and rejects unrepresentable
  characters with `EILSEQ`. ICU provides the engine's Unicode facilities.

LLVM 22.1.0 compiler-rt supplies the needed integer and binary128 arithmetic.
The AArch64 floating-point-mode implementation reads the actual FPCR/FPSR.
`tools/build_user_jsc_support.py` also builds pinned libc++ `hash.cpp` and
`functional.cpp`, retaining actual out-of-line hash calls in its test.

After generating a completed native SDK report and preparing its dependencies:

```sh
python3 tools/prepare_user_builtins.py --fetch
python3 tools/build_user_libm.py --c-support --fixture-runtime-report build/jsc-dihos/report.json
python3 tools/build_user_jsc_support.py
python3 tools/build_user_stdio.py
```

The final two commands depend on the earlier archives; run them in order.
The complete QEMU test command on Windows is:

```powershell
.\tools\test_user_runtime_qemu.ps1 -Python python -WebkitSync -WebkitRandom -WebkitVm -Rtti -FeatureMain -Libm -JscSupport -Stdio
```

The [initial 33-group result](browser-evaluation/native-stdio-20261030.json)
records the C-layer experiment before moving error storage into the shared SDK.
It verifies embedded ELFs and covers formatting/truncation, hexadecimal/decimal
and exceptional numeric parsing, scanning, denied files, stream state, concurrent
output, per-thread errors and repeated cleanup. The arithmetic fixture checks
128-bit division against an independent Python-integer oracle and retains actual
binary128 helper calls. These focused tests do not establish complete C-library
conformance or JavaScript execution. The
[shared-SDK follow-up](browser-evaluation/native-c-runtime-20261030.json) repeats
all 33 groups with error storage in the core runtime and consistent native FILE
declarations. Rebuild ICU and the engine after a shared SDK ABI change, then
repeat the execution tests.

The embedding linker accepts all verified dependency reports:

```sh
python3 tools/build_jsc_native_test.py --libm-report build/user-libm/report.json --support-report build/user-jsc-support/report.json --stdio-report build/user-stdio/report.json
```

It preserves real link failures and checks any resulting ELF against the
production loader. Packaging and guest engine execution remain separate gates.
The linker freshly compiles the embedding using the verified probe's target
command, checks its real C API imports and records its object hash. Changing
only that fixture does not require rebuilding the unchanged engine archives.

The experimental engine execution test adds `-Jsc` to the QEMU command. It
uses a separate 512 MiB test guest and an owned, incremental ELF load. The
test's explicit interpreter profile requests an 8 MiB structure heap through
JavaScriptCore's upstream options-customization callback. The default starts
at 4 GiB and its eight allocation attempts never reach a size compatible with
the current 16 MiB native region limit, which includes alignment padding.
This profile preserves StructureID alignment and all upstream assertions;
it does not establish sufficient memory capacity for modern websites.

For focused reruns, `-ReusePrograms` verifies the base program sources, pinned
dependency tree, compiler binaries and all built ELF/archive hashes before
skipping their rebuild. The production kernel test harness and selected
embedding are still rebuilt. Any base-input change requires a run without
that switch. Test-executable abort diagnostics use only forward, aligned
frames within the kernel-reported thread stack and terminate the process.
