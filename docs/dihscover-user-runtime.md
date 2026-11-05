# ARM64 browser process foundation

This is the first implementation slice of the [web-app roadmap](browser-evaluation/webapps-roadmap.md). Dihscover still runs through its existing SACX/NetSurf path. A [restricted static ARM64 ELF loader](dihscover-user-elf.md) now extends the kernel-built fixtures. No replacement engine, dynamic linker, IPC broker, browser service or media decoder has been added yet.

## Implemented

An opt-in, single-core scheduler runs four kernel-built EL0 fixtures with private translation roots, RX code, RW stacks and inaccessible stack neighbors. It saves integer registers, all FPSIMD registers, floating-point controls and TLS across timer interrupts. SVE/SME are disabled for these tasks. Inherited mappings must be kernel-only before admission; exposed EL0 mappings cause admission to fail.

Only exit, yield and bounded sleep are supported at the syscall boundary. A sleeping task returns immediately to the desktop scheduler; no networking, filesystem operations or engine callbacks run inside this boundary. Each desktop iteration runs at most one 1 ms user quantum. Task faults release that task's owned pages and return to the kernel. Generation handles prevent stale cancellation from targeting a replacement task.

GICv3/v4 addresses and the virtual timer PPI come from ACPI MADT/GTDT. Normal boot does not enable this runtime. Explicit activation checks the existing timer, firmware GIC system interface and interrupt configuration, then verifies timer delivery using a finite user program before admitting an infinite loop. Unsupported platforms return an error. An unexpected IRQ stops the fixtures; general interrupt servicing remains future work.

This is not a complete application sandbox yet. The initial executable format admits only static, relocation-free ARM64 images through an explicit terminal command. Multi-core scheduling, shared memory/IPC, user file/socket services, runtime libraries, dynamic linking, engine integration and production wait/event services remain to implement.

## Host validation

```powershell
.\tools\test_user_runtime_qemu.ps1 -Python python
.\tools\test_user_runtime_qemu.ps1 -Python python -TimerEdge
.\tools\test_user_runtime_qemu.ps1 -Python python -TimerPageOffset
& 'C:\Program Files\LLVM\bin\clang.exe' -std=c11 -Wall -Wextra -Ikernel/include tools/user_runtime_acpi_tests.c -o build/user-runtime/acpi-tests.exe
.\build\user-runtime\acpi-tests.exe
.\tools\test_hyperv.ps1
.\tools\test_hyperv_package.ps1
```

The QEMU script needs LLVM/LLD, Python, Ubuntu WSL and `qemu-system-aarch64` in that distro (or `-Qemu` pointing to an executable). It also accepts the locally extracted QEMU runtime under `build/user-runtime/qemu-root`. No system package install is performed by the test script. The recorded integration run used QEMU 10.2.1, GICv3 and a Cortex-A72 CPU model.

Follow-up: the initial Hyper-V screenshot showed error `FFFFFFFF` before fixture admission, with GICv3 and timer PPI 20. The initial preflight rejected all edge-triggered GTDT timers. The driver now accepts both trigger modes and programs GICR_ICFGR1 from the firmware flag, following the [ACPI GTDT trigger interpretation](https://raw.githubusercontent.com/torvalds/linux/master/drivers/acpi/arm64/gtdt.c). Both level and edge configurations pass the emulator integration suite, including a check of the actual interrupt configuration register. The guest must still confirm its flags and successful timer delivery; the original screenshot did not expose those flags. Activation errors now distinguish missing ACPI information, invalid PPI, platform setup and timer delivery failure.

The next Hyper-V screenshot reported flags zero and a 128 KiB redistributor range, while still returning the original generic configuration error. Edge triggering was therefore not the cause of that failure. The displayed nonzero, aligned addresses and PPI 20 pass the extracted production validation checks. The diagnostic build now records the actual configuration received by the driver and its preexisting ready/core state, with separate errors for each invalid field. The unexplained guest rejection remains open pending that evidence; no Hyper-V timer success is claimed. Run `tools/user_timer_config_tests.c` linked with `kernel/asm/aa64/aa64_user_timer.c` as a host test to check those values and every invalid-field error.

The precise diagnostic subsequently returned `-116`: the actual redistributor address is `0xEFFEE000`, not the initially transcribed `0xEFEE0000`. It ends in `E000`, which fails the original 64 KiB alignment guard. The driver now preserves that exact ACPI address and accepts 4 KiB alignment; range, overflow, register-version and CPU-affinity checks remain. The configuration host test now uses the exact guest address. `-TimerPageOffset` models that alignment over QEMU's fixed GICR device through a test-only MMIO address adapter; production address arithmetic is unchanged. The integration suite passes with it. This establishes the alignment-check correction, not yet timer delivery on Hyper-V. The guest also froze during terminal resizing before any EL0 fixtures started; that separate failure remains unresolved. Status output is shorter so collecting it does not require resizing.

The following guest log contained a rejected finite-probe slot with zero slices. That proves admission was attempted after timer initialization; the shared `-5` error obscured the failure stage. Admission now reports a distinct `-200` top-level result and preserves its exact step and underlying error (allocation, VM creation, address translation/search, mapping or supervision). Zero-slice admission failures are printed as such rather than showing meaningless PC/ESR/FAR values.

The next guest log identified `unmapped address search` as the failing step. The fixture searched only 64 candidate aliases, all already mapped by firmware. It now prefers spare aliases but can use its own allocated backing pages in the private VM when no candidate is free. The existing VM mapper verifies that any replaced inherited translation resolves to exactly the owned physical page. Each code and stack allocation owns three pages and exposes only its middle page to EL0, leaving both neighboring pages inaccessible to users. Six payload/guard pages plus the maximum 24 table pages fit within the 128 KiB task budget. These pages are reclaimed on every exit, cancellation and failed admission.

The ARM64 emulator test now maps the entire 39-bit TTBR0 address space before exercising the runtime. Admission, preemption, sleep, faults, cancellation, allocation failure and repeated create/close pass through the backing-address path. Additional actual EL0 tests verify code executes, RX writes fault, stack execution faults, upper/lower stack accesses fault, and original kernel descriptors remain unchanged by private table splits. A sparse-map check also verifies that the original spare-alias path is retained. Hyper-V still needs to prove timer delivery after this admission correction; resizing remains untested in the latest user report.

Investigation also found that `pmem_init` reclaimed ARM64 BootServicesCode/Data even though `exit_boot_and_jump` calls the kernel directly on its inherited stack and DihOS retains the active firmware translation tables. ARM64 now keeps those firmware allocations reserved; it allocates ordinary pages from ConventionalMemory and retains the explicit loader-reserved executable pool. x64 policy is unchanged. This reduces free RAM until owned boot stacks/page tables permit selective reclamation. `tools/firmware_memory_tests.c` exercises the actual allocator against Boot Services and conventional descriptors and verifies that no allocation consumes firmware pages. The existing 32,000-allocation coalescing stress test and page-offset ARM64 runtime integration pass. This fixes a demonstrated ownership error; a causal link to the user's resize freeze remains an inference until a guest retest.

The actual production timer, context-switch assembly, private VM and scheduler run inside the emulator. Checks cover 100 timer preemptions of an API-free loop, full register/TLS restoration, kernel-root restoration, compatibility with the existing synchronous EL0 entry/exit, user exit, faults, RX-write and stack-execution protection, nonblocking sleep, cancellation, exhausted slots, allocation failures, stale handles and 100 create/close cycles. ACPI host fixtures cover boot-core selection, redistributor-region fallback and truncated entries. Logs are under `build/user-runtime` and `build/user-validation`.

`tools/export_user_snapshot.py` exports the published browser baseline and an explicit kernel overlay into `build/user-validation/source`, reconstructing only the new shell commands. It intentionally refuses to overwrite an existing snapshot. The unrelated native GPU/ripple/Wi-Fi changes and working-tree kernel binary are excluded. The kernel can then be built with:

```powershell
python tools/export_user_snapshot.py
.\build\user-validation\source\build.ps1 -ProjectRoot .\build\user-validation\source -ArmOnly -KernelAa64Out build/user-validation/KERNEL.ELF
```

## Hyper-V validation, still required

The owned-address fixture package passed on Hyper-V: the API-free loop was preempted 6,607 times, the ordinary task exited with 77, the deliberate null-access fault was contained, the sleeping task exited with 88, and `proc:stop` reported cleanup. This validates the timer/task fixture gate on that guest. It does not establish native-hardware behavior, measured desktop latency, general application loading or resize stability. The later ELF package requires its own guest test.

The diagnostic package is `build/dihos-user-runtime-owned-va-20261028`, build `20261028T141831Z-6F7C21FEFF50`. Its 54 manifest files and hashes were verified, and its browser binary matches the previous working package. The source and validation records are in `build/user-validation-owned-va/source.json` and `result.json`. The VHDX has not been updated by this work. This replaces the earlier packages whose preflight failed in Hyper-V; it includes the firmware-memory reservation, redistributor alignment correction and owned-address admission fallback; Hyper-V timer delivery remains unverified.

Turn the VM off, then run this from the repository in Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-runtime-owned-va-20261028
```

The update uses the default DihOS VM/VHDX paths and preserves files outside the system manifest. After booting, run:

```text
proc:status
proc:test
proc:status
proc:stop
```

While the fixtures run, move windows, type in the terminal and scroll. The loop task should remain ready with increasing preemptions; the exit task should show exited, the deliberate fault task faulted, and the sleeping task should eventually exit. `proc:stop` cancels the loop and releases its pages. If activation fails, photograph `proc:status` and the activation error. Reboot before retrying a platform initialization failure.

Passing the emulator does not establish Hyper-V timer delivery, native-hardware behavior or desktop latency. No p95 input latency or frame-rate acceptance result is claimed yet. Keep the previous working package for comparison.

## Next dependencies

Validate this fixture in Hyper-V, then implement executable loading, owned syscall buffers, event waits, IPC/shared surfaces and the target runtime needed by the selected engine. Continue the pinned engine feasibility work separately. Future media may receive and process audio tracks while discarding their output; absence of a sound driver must not stall the media clock or video.
