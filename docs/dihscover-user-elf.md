# Static ARM64 user images

Guest update: the synchronized-switch build passed on Hyper-V. The exit ELF returned `0x2A`, and the loop remained runnable through 55 timer preemptions before `proc:stop` reported cleanup. This establishes the reported guest execution check, not native-hardware validation or general stability. The next step is the [SACX userspace bridge](dihscover-sacx-userspace.md).

The next OS foundation after the Hyper-V task tests is an independently compiled executable path. `proc:run <path>` now queues an ELF file, validates it, copies it into kernel-owned allocations and starts it as a preemptible EL0 task. Existing Dihscover/SACX entry points are unchanged. This is a small development format, not a Linux binary ABI or a browser-engine port.

## Format and lifetime

The loader accepts little-endian ARM64 ELF64 ET_DYN images with at most 16 program headers and eight non-overlapping load segments. Files and virtual image spans are capped at 256 KiB. Images begin at relative virtual address zero, use page-aligned load segments, require readable mappings and forbid writable executable pages. Entry must be instruction-aligned and inside file-backed executable bytes.

Dynamic/interpreter/TLS program headers and nonempty relocation sections are rejected. The linker recipe asserts that no relocations are emitted. There are no imports, argv/environment, user heap, libc, C++ runtime or dynamic linking yet. The entry has a 16 KiB stack and can use the existing exit/yield/sleep SVC numbers 0/1/2 with x8 as the number and x0 as the argument. A program must exit through SVC rather than returning to a kernel caller.

Each image owns its allocation, with kernel-only guard pages on both sides, and each stack owns four writable pages plus two kernel-only guards. Only load-segment pages and stack payload pages become accessible to EL0. The VM mapper checks that inherited translations being replaced point to the task's own backing pages. Code pages are RX, data pages RW or R, and stacks are NX. File staging memory is freed before first execution, and faults, cancellation and failed admission release private tables, image and stack allocations. Per-task worst-case backing/table allocation fits within 512 KiB; one bounded staging job can additionally own up to 256 KiB.

## File scheduling and limits

Queueing owns the path and returns without reading a file. The desktop poll opens it, then reads at most 4 KiB per iteration; short reads are handled. The file is closed and staging memory released after admission or failure. `proc:stop` also cancels a queued/read-in-progress load. A second load is rejected while the one staging job is active. `proc:status` reports load state (1 queued, 2 reading, 3 admitted, 4 failed, 5 cancelled), errors and task execution state.

The underlying filesystem/virtual-disk calls remain synchronous kernel calls. Chunking bounds scheduled byte work; it does not guarantee a latency bound if a disk operation stalls. Admission/copy/mapping is bounded by the image cap but still runs at EL1 in one poll. This milestone makes no p95 latency claim. Image-format validation is not signature verification or a completed hostile-application sandbox. Run the supplied development programs; broader admission/security work is still required.

## Build and test

```powershell
.\tools\build_user_programs.ps1
python tools/embed_user_programs.py
& 'C:\Program Files\LLVM\bin\clang.exe' -std=c11 -Wall -Wextra -Ikernel/include -Ibuild/user-runtime tools/user_elf_tests.c kernel/process/dihos_user_elf.c -o build/user-runtime/elf-tests.exe
.\build\user-runtime\elf-tests.exe
& 'C:\Program Files\LLVM\bin\clang.exe' -std=c11 -Wall -Wextra -Ikernel/include -iquote include -Ibuild/user-runtime tools/user_loader_tests.c kernel/process/dihos_user_loader.c -o build/user-runtime/loader-tests.exe
.\build\user-runtime\loader-tests.exe
.\tools\test_user_runtime_qemu.ps1 -Python python -TimerPageOffset
```

The QEMU script rebuilds the three actual ARM64 ELF fixtures and embeds their exact bytes in the test kernel. It verifies initialized data/BSS, yield/sleep/exit, preemption of an API-free loaded loop, contained null faults, independence from staging-buffer lifetime and cleanup across allocation failures. Existing context, guard, mapping and scheduler tests remain. Host tests reject malformed headers, wrong architecture, truncated/overflowing spans, W+X, overlapping page ranges and invalid entry points, and exercise 10,000 input mutations. File-job tests cover owned paths, partial reads, missing files, EOF, OOM, busy admission and cancellation.

## Guest check

The first ELF guest run did **not** pass execution: both exit and loop images were admitted but faulted at entry+4 while reading image+0x1000 (`ESR=0x9200000E`). The OS contained those faults. A level-2-table QEMU layout alone did not reproduce the guest symptom. Investigation found two private-mapping preparation bugs: dirty private descriptors were not explicitly published before activation, and cloning/splitting could retain firmware contiguous hints while changing members of their groups. The follow-up clears those hints only in copied/split private tables and cleans owned descriptor pages before first entry. Initialized image/stack data is also published. On another fault, status retains TCR and the four raw descriptors for FAR before freeing the private VM. Guest resolution remains unverified.

The expanded QEMU `-FirmwareL2` mode seeds level-2 firmware blocks with contiguous hints. Tests verify that modified private trees no longer retain those hints, split guard leaves have no hint, and original firmware descriptors retain theirs. The compiled ELF/context/isolation/cleanup suite passes in this layout. QEMU does not prove physical cache behavior. ARM64 [contiguous mappings require consistent group management](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/80119a77e5b03747b8886505df1b3cb26f49168d/arch/arm64/mm/contpte.c); the precise cause of this guest fault is still an inference pending the retest.

The follow-up guest still faulted on the same read. Its software walk reached L3 descriptor `0x006000007EB2274F`, with EL0 RW access, while ESR reported an L2 permission fault. TCR `0x0000000280803518` selects a 40-bit TTBR0 space, which is now covered by QEMU `-FirmwareL0` in addition to the L2/contiguous-hint layout. That test passed before and after the following activation changes; it does not reproduce the Hyper-V symptom.

The new switch sequence synchronizes each TTBR0 write immediately with ISB, then uses VMALLE1IS plus DSB/ISB on both entry and return. User leaves are non-global. No permissions are relaxed. Fault capture records the live TTBR0 and SCTLR before restoring the kernel root, and runs a read-only `AT S1E0R` probe for FAR while the user root is installed, retaining PAR and restoring its previous value. Tests verify that a deliberate fault records the expected private root and a failing hardware probe. Root-write synchronization follows the ordering illustrated by [ARM64 root replacement](https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/mm/proc.S). This is a tested activation correction and additional evidence collection; guest resolution remains pending.

Prepared package: `build/dihos-user-elf-switch-20261028`, build `20261028T150847Z-CB40DAF54ADD`. All 58 manifest files/hashes were verified; the three ELF hashes match the tested fixtures, and the browser binary matches the working baseline. Kernel source, program-source hashes and validation records are under `build/user-validation-elf-switch` and the packaged `UserRuntime/SOURCE.JSON`. The VHDX has not been updated automatically.

With the VM off, update from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-elf-switch-20261028
```

To reproduce packaging after exporting/building a fresh kernel snapshot, build the fixtures with `tools/build_user_programs.ps1`, run `python tools/package_user_programs.py <snapshot-directory>`, stage the validated Dihscover binary as before, and use `tools/hyperv.ps1 -Action Package` with that snapshot. The helper copies the fixture source recipe into the snapshot and records source/program hashes.

After installing the new package, run one image at a time:

```text
proc:run 0:/OS/System/Programs/UserRuntime/exit.elf
proc:status
proc:run 0:/OS/System/Programs/UserRuntime/loop.elf
proc:status
proc:stop
proc:run 0:/OS/System/Programs/UserRuntime/fault.elf
proc:status
```

Allow a few desktop frames for loading. The exit image should show exited with status `0x2A`; the loop should gain preemptions while pointer/terminal remain usable; stop should reclaim it; the fault image should show a contained fault at address zero. Save the output with `flush`. Guest ELF validation is still pending. Native hardware and resize stability also remain unverified.

Follow-up: the user confirmed the earlier ELF exit/loop checks on Hyper-V. The
[SACX bridge](dihscover-sacx-userspace.md) and normal [heap fixture](dihscover-user-memory.md)
also passed the reported guest runs. The new [freestanding C++ startup and bounded
relative relocations](dihscover-user-cpp.md) pass QEMU; that guest check is pending.
Wait/event services, IPC/shared surfaces and the full target libraries/engine
dependencies remain. The current format still cannot load WebKit, Servo or Ladybird.
