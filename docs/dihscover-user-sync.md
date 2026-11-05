# SACX-backed C++ synchronization

The selected adapter supplies `std::mutex`, `std::recursive_mutex`,
`std::call_once`, `lock_guard`, `unique_lock` and `scoped_lock` using unmodified
pinned libc++ 22.1.0 headers. DihOS implements their out-of-line ABI and external
threading interface; no host pthread library is linked into the guest.

## Build boundary

This uses a separate site profile, `build/user-libcxx-sync-config`. The original
container/string library retains its thread-disabled profile. Configure the
header project with `-DDIHOS_SYNCHRONIZATION=ON`, and follow the exact compiler
flags and link recipe in `tools/build_user_sync.ps1`. Do not mix profiles within
an application. This is a selected library slice, not a full threaded libc++.a;
the feature bits select header code, not a promise that every symbol exists.

At this original synchronization milestone, absent interfaces included `std::thread`, POSIX thread-specific keys, standard
`std::condition_variable` out-of-line/timed operations, `std::chrono` clock
definitions, futures and remaining libc++/WTF adapters. Missing symbols fail to
link; they have no success stubs. The fixture uses native SACX workers and
native condition predicates. JavaScriptCore does not run in DihOS yet.

The subsequent [standard-thread/key adapter](dihscover-user-stdthreads.md) adds
standard workers, native generation-checked keys, monotonic sleep/clock and
untimed standard conditions. Consult its limits before using those interfaces.

## Synchronization contracts

`sdk/sacx/include/sacx_user_sync.hpp` provides native mutexes, recursive mutexes
and conditions. Contended locks park through WAIT32, with acquire/release
ordering and checked ownership. Unlock releases the word before waking a waiter;
recursive depth overflow returns an error. Condition waits capture a sequence
before unlocking, then compare/enqueue atomically, so notifications between
unlocking and parking are not lost. Callers must loop on a predicate while
holding the same mutex. Spurious notifications are permitted.

Uncontended lock identity comes from the loader-installed TCB copy, without a
syscall or voluntary scheduler switch. This user-writable value is library
bookkeeping, not authority: kernel thread operations still use private context.
The initial adapter queried identity through a syscall inside each lock/unlock.
Under guest scheduling, two libc++ multi-lock attempts could repeatedly switch
between competing partial acquisitions without making progress. The reported
Hyper-V run exposed this with one joined worker and over 55,000 calls. It was
not a successful guest validation.

Condition deadlines use absolute monotonic milliseconds. Timeout and ordinary
wakes reacquire the mutex; a mutex error is returned, not hidden. A 60-second
kernel timeout merely causes an indefinite wait to recheck and continue. A
waiter count rejects destruction while waits remain. Objects must outlive all
users: copying active locks, concurrent destruction and address reuse during a
wait are invalid. Individual worker cancellation remains unsupported.

`std::call_once` publishes libc++'s completed 64-bit state with release/acquire
ordering and parks competitors on its low word. Exceptions remain disabled;
exception rollback is not claimed. Callbacks run in preemptible EL0. Invalid
standard mutex operations terminate only their process with `0x9C`.

## Tests and guest gate

Both ARM64 QEMU layouts pass three repetitions of `sync.elf`. Main and three
workers contend on a slow once callback, preserve 240 shared updates using
multi-mutex `scoped_lock` while yielding inside critical sections, test recursive
locking, condition broadcast/timeout/reacquisition, and join all workers.
`sync_invalid.elf` aborts only its process. Earlier runtime fixtures pass and all
tests reclaim their process pages.

The correction also runs three repetitions with 17 ms of simulated desktop time
between user syscalls. The old adapter fails this regression; the TCB fast path
passes both fast and frame-paced scheduling. The simulation advances only the
test clock and bounds the number of polls, rather than delaying every emulator
iteration or changing the production scheduler. The 240-update fixture remains
unchanged in its contention requirements.

The host test injects an atomic compare/enqueue backend instead of calling SACX
into Linux. Four concurrent host threads perform 40,000 protected updates and
recursive locks, plus ownership, timeout and 100 notification-race checks.
Address/undefined-behavior sanitizers and leak checks pass. This supplements EL0
tests, not a claim of multicore guest execution.

```powershell
.\tools\test_user_runtime_qemu.ps1 -Python python -TimerPageOffset -FirmwareL2 -FirmwareL0
wsl -d Ubuntu -- sh tools/test_user_sync_host.sh
```

Corrected package: `build/dihos-user-sync-progress-20261028`, build `20261028T195013Z-FE830D35E0AA`.
All 80 manifest files/hashes were verified. Sources and validation logs are in
`build/user-validation-sync-progress`; the working-tree kernel is preserved.
The package reuses the previously compiled/validated kernel and boot files after
verifying unchanged kernel/syscall sources; `kernel-reuse.json` records their
origin and hashes. With the VM off, run from Administrator
PowerShell in the repository:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-sync-progress-20261028
```

After boot, run `proc:run 0:/OS/System/Programs/UserRuntime/sync.elf`, allow about
10–20 seconds, then `proc:status`. Expect `exited`, status `0x53`, three joined workers,
zero live workers/remaining heap, and
`SACX libc++ sync: mutex, recursive mutex, call_once and condition waits OK`.
Optional `sync_invalid.elf` deliberately exits `0x9C`. New guest validation is
pending. The working browser is retained; packaging does not update the VHDX.

The corrected synchronization package passed the reported Hyper-V retest: exit
`0x53`, all three workers created/exited/joined, zero live workers and the
expected synchronization message. The original livelock is resolved in that test.
