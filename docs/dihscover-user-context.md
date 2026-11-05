# Native thread inspection, clocks and floating-point support

This layer supplies real saved EL0 contexts to the experimental WebKit port.
It does not expose kernel mappings, a signal ABI or a register-restore syscall.
The existing ABI envelope remains version 1; operations are appended and
advertised by `QUERY` without changing its structure.

| Operation | Number | Grant | Behaviour |
| --- | --- | --- | --- |
| `THREAD_PAUSE` | 18 | Threads | Stop a live, different thread in this process and copy an 848-byte versioned context to checked writable user memory. |
| `THREAD_RESUME` | 19 | Threads | Release the calling inspector's stop on the generation-tagged target. |
| `THREAD_STACK` | 20 | Threads | Return owned stack bounds for a live process-local thread, including self. |
| `THREAD_DETACH` | 21 | Threads | Relinquish a worker's join; reclaim its stack/TLS backing after its runtime exit hooks finish. |
| `UTC_MS` | 22 | Clock | Return firmware-anchored UTC milliseconds, or `-61` when unavailable. |
| `PROCESS_SELF` | 23 | None | Return this process's generation-tagged debug identity, without authority over another process. |
| `RESOURCES` | 24 | Clock + Memory | Copy an 80-byte versioned snapshot of the calling thread/process CPU counters and private backing/heap counts to checked writable user memory. |

Only the boot core currently executes user slices, so the service sees a stable
saved frame after returning to the kernel translation root. A successful stop
retains the original runnable, sleeping, joining or word-wait state. Stopped
threads neither execute nor complete waits until resumed. Notifications to a
stopped word waiter are retained exactly once and applied on resume. Sleep/join
deadlines remain absolute. A failed destination check acquires no stop.

There is one inspector per process, preventing competing inspectors from
stopping each other. Self-stop and recursive stops are rejected. Only the
inspector can resume targets. Inspector thread exit automatically releases its
stops; process exit, fault and cancellation reclaim all owned backing. This is
intraprocess authority under the existing thread grant, not access to another
process. Stop every other mutator before scanning an entire managed heap.

The copied context contains the actual GPRs, SP, PC, PSTATE, full SIMD register
bank, FPCR, FPSR, user TLS pointer and owned stack bounds. Editing the copy has
no effect on the scheduler's private context. WebKit's native `PlatformRegisters`
uses these fields and the C-loop stack-pointer interface. It does not claim
`HAVE(MACHINE_CONTEXT)` or construct a POSIX `ucontext_t`.

Native ELF main/worker stacks now have a 1 MiB payload, guards and separate TLS.
The process quota charges space for all four thread backing allocations before
calculating heap capacity. The small trusted timer fixtures retain their own
layout. Custom-stack placement and real-time scheduling remain unsupported.

`fenv.cpp` operates on architectural FPCR/FPSR. Rounding modes and exception
flags survive thread switches; new workers inherit the creator's FP environment.
Environment restoration rejects enabled exception traps because this runtime
does not supply FP signal handlers. `abort()` terminates the isolated process.

WTF wall time now comes from `UTC_MS`, while monotonic and elapsed clocks use
the architectural monotonic service. Missing UTC fails the app explicitly;
uptime is never presented as a calendar date. Precision is milliseconds. Guest
suspend/power-state semantics are not implemented.

The native C calendar implementation uses the proleptic Gregorian calendar and
UTC for both `gmtime` and `localtime`; the OS has no timezone preference yet.
It supports signed timestamps, normalizes `mktime`/`timegm` inputs and rejects
results outside the representable `tm_year` range. Non-reentrant calendar
functions keep their result in thread-local storage. This supplies real date
conversion for the engine without claiming a POSIX timezone service.

## Validation

```powershell
.\tools\test_user_runtime_qemu.ps1 -Python 'C:\Users\joshu\AppData\Local\Programs\Python\Python312\python.exe' -WebkitSync -WebkitRandom -WebkitVm -Rtti -FeatureMain
```

The ARM64 EL0 suite exercises actual GPR/SIMD/FP sentinels in an API-free worker,
two simultaneously stopped mutators, held-state stability, rejected destinations and stale handles, competing
inspectors, deadlines, notifications, inspector exit and cancellation. The
platform fixture verifies real rounding instructions in three threads,
96 KiB stack frames, UTC/monotonic progression and 100 detached-worker reclaims.
Both fixtures run four times and require all tracked pages to be returned.
Calendar tests additionally compare 1,084 independently generated UTC vectors,
leap centuries, negative timestamps, JavaScript date limits, normalization,
overflow rejection and per-thread result storage.

The actual WTF synchronization and native clock adapters also execute in EL0,
including missing-UTC failure, finite/infinite deadlines and TLS cleanup.
The full WTF `Thread` object adapter is separately cross-compiled; running it
still requires completing the WTF/JSC link. No runnable JSC engine or JavaScript
website support is inferred from these component tests. Hyper-V validation of
the new operations remains pending.
