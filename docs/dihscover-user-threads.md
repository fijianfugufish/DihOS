# Native SACX threads, local-exec TLS and timed waits

The isolated ARM64 runtime now supports up to four threads in a process: the
main thread and three joinable workers. They share the process image/heap/VM;
each has separate CPU/FPSIMD state, a guarded 16 KiB stack and an owned TLS page.
Only the boot core executes user code. This is time-sliced concurrency, not a
multicore userspace or a completed pthread/`std::thread` port. Libc++'s thread
feature remains disabled until its platform adapter and remaining ABI work exist.

## SACX interface and ownership

The existing version-1 envelope remains compatible. Operations append at 10–15;
QUERY advertises `0xffff`. The kernel checks thread capability bit 7 before
dispatch; development ELFs now receive clock/log/memory/thread grants `0xE4`.
There is no change to user file permissions in this step.

| Operation | Arguments | Result/behavior |
|---|---|---|
| THREAD_CREATE (10) | RX entry, initial x0, initial x1 | Positive process-local generation handle, or negative error; kernel owns the stack/TLS |
| THREAD_SELF (11) | Zero, zero, zero | Current thread's authoritative kernel handle |
| THREAD_EXIT (12) | 64-bit result, zero, zero | End the worker; main uses existing process EXIT |
| THREAD_JOIN (13) | Handle, optional result pointer, timeout ms | Zero after copying the full 64-bit result and reaping; zero timeout is try-join |
| WAIT32 (14) | Aligned readable word, expected value, timeout ms | Park only if the word still equals expected; mismatch returns `-11`, timeout `-110` |
| WAKE32 (15) | Aligned readable word, count 1–4, zero | Wake up to that many waiters in this process and return the number |

`sdk/sacx/include/sacx_user_threads.h` supplies a start trampoline and native
wrappers. The callback returns a raw 64-bit value; the trampoline uses THREAD_EXIT.
Worker entry must be an aligned EL0-executable address. Non-executable data and
kernel addresses are rejected. No caller-supplied physical address, kernel
callback, stack mapping or page-table pointer is accepted.

Timeouts are bounded to 60 seconds and are scheduler deadlines, not hard realtime
guarantees. Wait comparison/enqueue and wake dispatch are serialized while user
threads are paused, preventing a check/enqueue lost-wakeup window. Callers still
use atomic predicates and release/acquire ordering around their protocol. Wait
keys and handles are process-local. The same numeric handle/address in another
process cannot operate on this process's threads or waiters.

Join rejects self/main/stale handles, duplicate joiners and bad output buffers.
Blocking output is revalidated on completion; a failed output copy leaves the
completed worker available for a valid join retry. Result bytes remain unchanged
on timeout. A wait word made inaccessible while parked returns `-14`; no physical
pointer is retained to freed user memory. Applications must still obey normal
object lifetime/address-reuse rules for synchronization words.

## TLS and resource lifecycle

The loader accepts one bounded PT_TLS template with up to 4,080 TLS bytes and
power-of-two alignment up to 16. Initialized data and BSS-only TLS are supported.
It takes an immutable kernel copy of the initial bytes; changing main's TLS
does not change newly created workers' initial values.

The [AArch64 TLS ABI](https://github.com/ARM-software/abi-aa/blob/main/sysvabi64/sysvabi64.rst)
defines a 16-byte TCB at TPIDR_EL0 followed by the static TLS block. The restricted
profile's alignment makes the padding zero. The first TCB word remains reserved
for unsupported dynamic TLS; the implementation word holds a user-visible handle
copy. Changing that copy cannot change kernel identity or authority.
Compiler-generated local-exec access uses the per-thread TPIDR_EL0 saved/restored
by the existing slice code. TLS relocations/dynamic modules, oversized/aligned
templates and fixups overlapping the TLS template are rejected. The subsequent
[C++ lifetime layer](dihscover-user-lifetime.md) adds nontrivial thread-local
destructors and thread-safe local-static guards. POSIX TLS keys remain absent.

Each thread backing block contains guard/stack/guard/guard/TLS/guard pages.
New mappings are prepared before exposing pages. A joined worker's mappings are
revoked and published before returning its backing to the allocator. Handles
advance generations on reuse. The process budget precharges all possible worker
stacks/TLS/guards and private tables before calculating the heap allowance.
Completed workers retain resources until join or process teardown.

The scheduler runs at most **one 1 ms user quantum total per desktop frame**,
round-robin across processes and then their runnable threads. Creating threads
does not multiply the desktop's user-code budget. Sleep/join/wait park a thread
and return immediately to the kernel loop. Main process exit/stop or any worker
fault discards the shared VM and reclaims every thread and heap reservation.
Individual worker cancellation/detach and per-thread fault recovery are absent.

## Allocator synchronization and validation

The TLSF adapter now uses an acquire/release mutex. Its uncontended path stays
in user space; contention parks through WAIT32 and unlock wakes a waiter. Nested
allocation paths use internal unlocked functions to avoid recursive-lock deadlock.
This does not make all other C/C++ runtime facilities generally thread-safe.
The [subsequent lifetime layer](dihscover-user-lifetime.md) adds compiler guards
and destructor synchronization; remaining libraries still need their adapters.

Sanitized native host tests retain 30,000 randomized allocator operations and add
four concurrent workers performing 40,000 allocations, with data/integrity/cleanup
checks. ARM64 QEMU passes default and Hyper-V-style layouts and exercises:

- initialized and BSS-only compiler TLS, distinct thread/stack identities and
  correct TLS reset across repeated generation reuse;
- timed join/wait, word mismatch, stale/self handles, thread limits, full-width
  return values, late output permission changes and invalidated wait words;
- shared allocator contention, a worker fault with heap cleanup, cancellation
  while four threads are parked, and backing-allocation failure;
- three API-free worker loops that must be preempted while main sleeps, resumes
  and joins them; earlier process/heap/C++/libc++ tests remain in the suite.

Host malformed-ELF tests also check invalid TLS offsets/sizes/alignment and
template-overlapping fixups. Capability tests check all six native operations.

```powershell
.\tools\test_user_runtime_qemu.ps1 -Python python -TimerPageOffset -FirmwareL2 -FirmwareL0
wsl -d Ubuntu -- sh tools/test_user_allocator_host.sh
```

## Guest gate

Prepared package: `build/dihos-user-threads-20261028`, build
`20261028T185007Z-C4D1E0EFA91A`. The isolated ARM64 build, both QEMU layouts,
TLS/ELF validation, syscall-capability checks, sanitized concurrent allocator,
Hyper-V host and deployment tests passed. All 74 manifest files/hashes were
verified. Sources and logs are in `build/user-validation-threads`.
The VHDX has not been modified by packaging.

With the VM off, update from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-threads-20261028
```

Then boot and run one at a time:

```text
proc:run 0:/OS/System/Programs/UserRuntime/threads.elf
proc:status
proc:run 0:/OS/System/Programs/UserRuntime/threads_spin.elf
proc:status
flush
```

Allow a few seconds for each test; the desktop should remain usable. The first
should exit `0x4E`, with 25 worker creations/exits/joins and
`SACX threads: TLS, joins, timed waits and shared heap OK`. The second should exit
`0x50` with nonzero preemptions and
`SACX threads: API-free workers preempted beside main OK`. Heap reservations and
live threads should be zero afterward. The reported Hyper-V run passed both:
spin exited `0x50` with 24 preemptions and three joined workers; threads exited
`0x4E` with 25 created/exited/joined workers, timed waits and no remaining heap.
Native hardware validation remains pending. Optional `threads_fault.elf` deliberately faults its process;
`threads_wait.elf` deliberately remains parked until `proc:stop`.

This is a native primitive layer for the eventual JavaScriptCore/WTF adapter.
It does not yet enable standard C++ threads, ICU, a larger engine loader/heap,
browser IPC or the replacement engine itself.
