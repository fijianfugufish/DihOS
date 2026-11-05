# First SACX userspace bridge

This is a small bridge for the browser runtime foundation. A separately loaded ARM64 ELF uses a userspace-local `sacx_api` table; implemented members issue SACX syscall requests instead of pointing into the kernel. Existing SACX applications and Dihscover still use their original runtime. This is not a complete SACX migration or a finished app sandbox.

## Implemented calls

`sdk/sacx/include/sacx_user_api.h` supplies `sacx_userspace_api()`. Its supported members are `app_yield`, `app_exit`, `time_monotonic_ms` and `log`; other members remain null and must be tested with `SACX_API_HAS`. The table and all of its function pointers live in the app's own mappings. It is initialized at runtime so the relocation-free ELF prototype can use it.

`sacx_user_syscall.h` defines the versioned transport: `svc #0`, x8=`0x53414358`, x7=ABI version 1, x0=operation, x1..x3=arguments. Results or negative errors return in x0. Other saved registers survive. Operations are query (0), yield (1), sleep in milliseconds (2), exit (3), monotonic milliseconds (4) and diagnostic log bytes (5). Query returns a fixed 32-byte record with version, implemented operation mask, granted capability mask and buffer limit. This transport version is distinct from the existing `sacx_api` table version.

SVC returns to the kernel scheduler before it dispatches a SACX service. No user callback or filesystem/network operation runs inside the exception handler. Each task has one outstanding request at a time and resumes with its result on a later slice. Sleep records a deadline instead of blocking the desktop. The older exit/yield/sleep trap numbers remain for the existing fixture controls during transition; new SACX applications use the envelope.

## Bounds and permissions

Clock and sleep require the kernel's CLOCK capability (`0x04`); logging requires LOG (`0x20`). Grants come from the process supervisor, not user registers, the API table or the query output. The development ELF admission policy currently grants these two capabilities (`0x24`); configurable manifests, grant UI and broader policy are deferred. Host tests exercise denied grants. There is no file, network, device, arbitrary kernel-call or graphics service in this initial bridge.

Payloads are capped at 256 bytes. User-copy helpers walk the paused process's private page tables and require EL0-readable/writable pages as appropriate, rejecting kernel-only pages, code-page output, guards and overflowing ranges. Output permissions are validated for the whole range before any bytes are written. The app cannot change its own page tables and is not executing concurrently with service dispatch. This is a single-thread/single-core assumption; future shared-memory/thread services must extend the ownership rules.

The service copies log text into kernel-owned storage. Only the latest bounded diagnostic message is retained per task and displayed by `proc:status`; this prototype does not stream every app log call to disk or the terminal. This keeps routine calls out of synchronous disk logging and unbounded terminal history growth. Unsupported operations return -38, wrong transport version -93, denied capability -1, invalid buffers -14, oversized payloads -7 and invalid arguments -22. The uint64-returning table clock wrapper returns zero on an error; low-level calls expose the signed error result.

## Validation

`tools/user_sacx_tests.c` links the actual service with bounded test-memory adapters. It tests negotiation, capability denials before buffer access, ownership of copied text, lengths, unknown operations and nonblocking sleep replies. The ARM64 QEMU suite tests the actual page-table copy helpers: normal round trips, kernel-page rejection, code-write rejection, wrapping ranges and cross-guard output that leaves the valid page unchanged.

The separately compiled `sacx.elf` uses the local API table, queries ABI information, checks unavailable file APIs, attempts null/wrapping/oversized buffers and code-page output, checks unknown-operation rejection, then exercises clock, yield, sleep, log and exit. It exits with 73 (`0x49`) only when all checks pass. The QEMU integration suite passes with the guest's 40-bit/L0-root layout, firmware L2 contiguous blocks and page-offset GICR model. Existing ELF, context, protection and cleanup tests remain in that run. The user also confirmed the Hyper-V bridge run: `exited`, twelve SACX calls, grants `0x24`, last result zero, and `SACX bridge: buffers, clock and sleep OK`. The guest status output did not separately print the exit status. The earlier ELF exit/loop gate has also passed on Hyper-V.

```powershell
.\tools\build_user_programs.ps1
& 'C:\Program Files\LLVM\bin\clang.exe' -std=c11 -Wall -Wextra -Ikernel/include tools/user_sacx_tests.c kernel/process/dihos_user_sacx.c -o build/user-runtime/sacx-tests.exe
.\build\user-runtime\sacx-tests.exe
.\tools\test_user_runtime_qemu.ps1 -Python python -TimerPageOffset -FirmwareL2 -FirmwareL0
```

After updating the package and booting, run:

Prepared package: `build/dihos-sacx-userspace-20261028`, build `20261028T155949Z-465CF5D74FC5`. The full ARM64 kernel build and all 59 manifest hashes passed verification. The existing browser and the three earlier ELF fixtures are unchanged; `sacx.elf` is added. Source/program hashes and the validation record are under `build/user-validation-sacx`. The VHDX has not been updated automatically.

With the VM off, update from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-sacx-userspace-20261028
```

Then boot and run:

```text
proc:run 0:/OS/System/Programs/UserRuntime/sacx.elf
proc:status
flush
```

Expected: exited with `0x49`, SACX calls greater than ten, grants `0x24`, and `SACX bridge: buffers, clock and sleep OK`. A load/admission success alone does not establish execution success.

This lays the buffer/authority boundary for future browser services. The subsequent [owned memory service](dihscover-user-memory.md) now supplies bounded reserve/commit/protect/release and local `mem_alloc`/`mem_free` wrappers. Its development grants are `0x64`; the `0x24` guest results above describe the earlier package. Working-directory file confinement, file/network handles, a full target allocator/runtime, threads, event waits, IPC/shared surfaces and the modern browser engine remain to implement. YouTube/video decoding is not part of this bridge.
