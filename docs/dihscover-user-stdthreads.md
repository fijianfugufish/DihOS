# Standard C++ threads and native thread-local keys

The selected libc++ 22.1.0 profile now runs `std::thread` on native SACX workers.
Upstream headers/templates remain unmodified; DihOS supplies their pinned ABI
adapter. This is still a selected library slice, not full libc++/pthread support
or a guest JavaScriptCore build.

## Supported contracts

- Standard thread creation, move-only callables/arguments, move assignment into
  an empty thread, identities, `join`, `joinable`, `swap` and destruction after
  join. The callback envelope is owned heap storage, freed by the child before
  invocation, or by the creator after admission failure. The ordinary libc++
  proxy owns the actual callable/argument tuple.
- Joining waits for C++ TLS destructors and native key destructors before the
  kernel marks a worker done. Each bounded kernel join timeout is retried; a
  standard join does not fabricate completion when 60 seconds elapse.
- `std::this_thread::yield`, `sleep_for` and `std::chrono::steady_clock::now` use
  SACX scheduling/monotonic time. Positive fractional-millisecond sleeps round
  upward. Clock resolution is milliseconds despite chrono's nanosecond type.
  Hardware concurrency reports one: only the boot core runs userspace.
- Untimed `std::condition_variable` predicate waits and notify operations,
  including `std::notify_all_at_thread_exit`. Up to eight deferred notifications
  per worker release their transferred mutexes and notify after C++ TLS cleanup.
  The process-lifetime libc++ thread-data key is initialized once, without a
  global destructor that could run while workers still use it.

`sdk/sacx/include/sacx_user_keys.h` provides native create/delete/get/set APIs.
Keys are process-local generation-tagged 32-bit values; zero is invalid.
The registry has 16 slots, including the libc++ internal key once first used.
Each thread's separate compiler TLS block stores values plus their generation.
Deleting/reusing a key cannot expose an old value. Deletion does not invoke its
destructor; callers own their values and must coordinate deletion with users.
An already selected destructor may finish after concurrent deletion.

After C++ thread-local destructors, key cleanup clears each value before calling
its callback, without holding the registry mutex. Reinstalled values receive up
to four passes. Remaining pointers are cleared afterward; callbacks that keep
reinstalling must manage their own pointed-to resources. There is no guaranteed
ordering between key callbacks, and callbacks should not construct new nontrivial
C++ TLS objects after the C++ cleanup phase. The per-thread key table adds 256
bytes to the existing bounded TLS template. Handles/grants/identity used by kernel
services remain kernel-owned; the key table holds no kernel pointers.

## Limits and failures

The native limit remains main plus three joinable workers. There is no detach
or individual cancellation. `std::thread::detach` reports unsupported through
the exception-free process-abort path; it does not leak a supposedly detached
worker. A joinable thread destructor calls the default `std::terminate` path.
Exceptions, exception handlers/unwinding and custom terminate handlers remain
outside this profile. Thread constructor/join errors abort only the process
(`0x9C`); unjoined-thread termination is `0x9E`; allocation abort is `0x7E`.
Faults and `proc:stop` reclaim owned memory without running arbitrary callbacks.

Realtime/system clocks, standard timed condition waits, futures/async, POSIX
thread APIs and the remaining WTF/platform facilities are absent. Unsupported
symbols fail to link; no success stubs stand in for them. The 256 KiB ELF image,
bounded allocator and single-core execution limits remain before JSC can fit.

## Validation and package

Both ARM64 QEMU layouts run the actual production VM/scheduler/syscall code.
Four standard-thread repetitions alternate fast and simulated 17 ms frame
scheduling. The fixture verifies move-only ownership, unique identities, 16
created/joined workers, C++-then-key cleanup, monotonic sleep, exit notifications
and repeated slot reuse. A separate fixture checks key capacity, stale handles,
thread isolation and the four-pass destructor bound. Negative tests exercise
the worker limit, unjoined destruction, unsupported detach and allocation
failure; all terminate only their process and release owned pages. Earlier
ELF/heap/C++/synchronization/preemption tests remain in the suite.

The positive fixture also checks zero live allocator objects after the first
worker group, the notification worker and every reuse join. It trims all cached
pools before the process exits. A handled low-level admission failure must free
its callback envelope and leave the output ID unchanged. These checks prevent
process teardown from hiding in-process leaks.

Sanitized host tests also run four concurrent key users through 4,000 generation
reuses and 1,600 destructor/reinstall callbacks, alongside 40,000 protected
updates and notification races. This tests native source with an injected
compare/enqueue wait backend, not SACX calls issued into Linux. It supplements
guest testing rather than claiming multicore guest support.

```powershell
.\tools\test_user_runtime_qemu.ps1 -Python python -TimerPageOffset -FirmwareL2 -FirmwareL0
wsl -d Ubuntu -- sh tools/test_user_keys_host.sh
```

Package: `build/dihos-user-stdthreads-checked-20261028`, build
`20261028T201022Z-FE830D35E0AA`. All 85 manifest files/hashes are verified.
The source snapshot, ARM64/host test logs and provenance records are retained in
`build/user-validation-stdthreads`. Kernel/boot artifacts are reused from the
validated synchronization build after verifying unchanged kernel/syscall
sources; `kernel-reuse.json` records their hashes and compiled source location.
The working-tree kernel remains untouched. With the VM off, update from
Administrator PowerShell in the repository:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-stdthreads-checked-20261028
```

Run `proc:run 0:/OS/System/Programs/UserRuntime/stdthreads.elf`, allow about
10–20 seconds, then `proc:status`. Expect **exited**, status `0x54`, 16 worker
creations/exits/joins, zero live workers and no reserved/committed heap, with
`SACX std::thread: ownership, keys, TLS cleanup, sleep and exit notification OK`.
Optional `keys.elf` should exit `0x55` after six joined workers, with
`SACX thread keys: isolation, generations, exhaustion and four-pass cleanup OK`.
The reported Hyper-V run passed: exit `0x54`, all 16 workers created/exited/joined,
expected success message and zero reserved/committed heap. The subsequent
[larger image/heap layer](dihscover-user-large-images.md) lifts the initial size
limits. The working browser is retained; packaging does not update the VHDX.
