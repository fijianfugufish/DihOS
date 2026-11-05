# C++ thread and static-object lifetime

This extends the native SACX thread layer with the compiler's AArch64 C++ ABI
hooks. It is a step toward JavaScriptCore's runtime, not a JavaScript engine or
a pthread/`std::thread` implementation.

## Behavior

- C++ local statics use `__cxa_guard_acquire/release/abort`: acquire/release
  publication, process-local word waits, retry after abort, and a diagnostic
  process exit (`0x99`) for recursive initialization. Other threads park while
  the constructor runs. Constructors and destructors still execute in EL0 and
  remain timer-preemptible.
- Compiler-generated nontrivial `thread_local` objects register per-thread
  destructors. They run in reverse registration order when a native worker
  returns or calls `sacx_user_thread_exit`, before its result becomes joinable.
  A destructor can register another destructor; the new registration is drained
  next. Main's TLS destructors run before process-wide static destructors.
- Global `__cxa_atexit` registration/finalization is synchronized. A callback is
  removed under the registry lock, then invoked without the lock. Competing or
  reentrant finalizers cannot invoke the same entry twice. DSO filtering works;
  dynamic loading/unloading itself remains unsupported.
- The runtime does not allocate memory to register TLS destructors. Its TLS
  registry takes 520 bytes out of the existing 4,080-byte TLS budget. Each thread
  supports 32 simultaneously pending destructor registrations. The low-level
  `__cxa_thread_atexit_impl` rejects a full registry with `-1`; the compiler-facing
  hook terminates the process (`0x9B`) on failure, since generated callers do not
  check the result. Global destructor capacity remains 32 entries.

The C++ build recipes enable local-exec TLS and compiler thread-safe static
initialization. The selected libc++ profile still disables its standard thread
API pending a platform adapter. POSIX TLS keys are not implemented.

## Exit boundaries

Use the native thread wrapper for normal worker exit. Calling the raw
`THREAD_EXIT` syscall bypasses C++ cleanup. Main returns from `dihos_user_main`
for ordinary process cleanup; `sacx_user_exit`, a fault, or `proc:stop` terminates
the process and reclaims its owned memory without running arbitrary remaining
destructors. Main exit does not join unfinished workers. Apps must join them
before returning. There is no individual thread cancellation or detach.

Exceptions remain disabled: guard abort is tested directly, not as evidence of
exception unwinding support. A user destructor that loops forever remains
preemptible, but cannot be promised to finish. Timeout waits retry their
predicate; they do not declare successful initialization after a timeout.

## Tests and package

`tools/test_user_runtime_qemu.ps1` runs the actual compiler-generated ARM64
programs against production VM, exception, scheduling and syscall code. Both
default and Hyper-V-style page-offset/firmware layouts pass. The suite repeats
the contended lifetime fixture five times and checks complete process resource
reclamation after each run. It also tests:

- three competing workers and main sharing a slow local-static constructor;
- initialized TLS ownership, reverse destructors, registration from a destructor,
  explicit worker exit, and join waiting for cleanup;
- competing global registry registration/finalization with yielding callbacks;
- guard abort/retry and isolated recursive-initialization failure;
- bounded registry exhaustion/reuse and 33 compiler-generated TLS registrations
  causing a controlled app abort rather than silently missing cleanup.

The earlier process, memory, allocator, libc++ and native thread fixtures also
remain in the suite. Host malformed-ELF tests accept the larger TLS template and
continue rejecting invalid metadata and overlapping fixups. No JavaScriptCore
guest execution or native hardware validation is claimed.

Package: `build/dihos-user-lifetime-20261028`, build
`20261028T192534Z-6710B9BEEC65`. All 78 manifest files and hashes were verified.
The source snapshot, build/QEMU logs, ELF/capability checks and deployment test
records are retained in `build/user-validation-lifetime`. Packaging did not
modify the VHDX or the working-tree kernel binary. With the VM off, run from an
Administrator PowerShell in the repository:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-lifetime-20261028
```

After booting, run `proc:run 0:/OS/System/Programs/UserRuntime/lifetime.elf`, allow
a few seconds, then `proc:status`. Expect `exited`, status `0x51`, three joined
workers, zero live workers/reserved heap, and
`SACX C++ lifetime: static guards, TLS destructors and concurrent finalization OK`.
The kernel retains the last log, so this message is emitted from the final
process destructor after all earlier lifetime checks.

Optional `lifetime_limits.elf` exits `0x52` after registry exhaustion/reuse checks.
`lifetime_recursive.elf` and `lifetime_overflow.elf` intentionally terminate their
own processes with `0x99` and `0x9B`; those are successful negative tests, not
kernel crashes. This package retains the working NetSurf browser. The reported
Hyper-V run passed: exit `0x51`, three created/exited/joined workers, nine waits,
nine wakes, the expected finalization message and zero reserved/committed heap.
The next [selected synchronization adapter](dihscover-user-sync.md) builds on it.
