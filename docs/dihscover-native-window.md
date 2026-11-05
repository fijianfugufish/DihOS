# Native WebCore window diagnostic

The local Page now uses SACX window syscalls to publish complete frames and
receive pointer input. This is a separate EL0 diagnostic, not a replacement for
installed Dihscover. It uses the same Page/JSC/Cairo/memory-font renderer as the
offscreen test. No user pointer or kernel function pointer reaches the compositor.

## Boundary and lifetime

Syscalls 33–37 append create, bounded frame upload, present, event and close to
ABI version 1. `sacx_user_window.h` contains the wire structs and local wrappers.
A kernel-selected compositor-surface grant is required; ordinary and network-only
launches have no window authority. `proc:run-ui <path>` grants an owned window
without network or file access.

The core-0 broker permits one window per process and four globally. Dimensions
are bounded by 1024 x 768. Two kernel-owned ARGB buffers cost at most 6 MiB per
window. Each syscall copies at most 1024 pixels (4096 bytes), using the existing
checked-copy limit. The kernel validates every user range; a frame becomes visible
only after consecutive uploads cover it completely. Ownership and generations
reject foreign/stale handles. Exit, fault and cancellation destroy the desktop
objects before releasing buffers. Surface authority grants neither MMIO nor
framebuffer access.

Input is queued in order, with 64 events per window. Adjacent motion can coalesce;
press/release transitions retain their positions. Overflow inserts an input-reset
event before new events so a slow app can release held state. The desktop adapter
routes the primary pointer using window focus, hit testing and capture, and queues
wheel and deferred-close events. WebCore receives its native mouse/wheel events.

## Test and build

Run the host broker and capability checks from the checkout:

```powershell
& 'C:\Program Files\LLVM\bin\clang.exe' -Ikernel/include -Iinclude tools/user_window_tests.c kernel/process/dihos_user_window.c -o build/user_window_tests.exe
.\build\user_window_tests.exe
& 'C:\Program Files\LLVM\bin\clang.exe' -Ikernel/include -Iinclude tools/user_sacx_tests.c kernel/process/dihos_user_sacx.c -o build/user_sacx_tests.exe
.\build\user_sacx_tests.exe
```

After completing the native engine build, run in ARM64 WSL:

```sh
python3 tools/webcore_dihos/compile_page_fixture.py --report build/webcore-engine-openssl/report.json --window
python3 tools/webcore_dihos/build_page_fixture.py --report build/webcore-engine-openssl/report.json --strip
```

Then in PowerShell:

```powershell
.\tools\test_user_runtime_qemu.ps1 -NativePage -Python C:\Users\joshu\AppData\Local\Programs\Python\Python312\python.exe
```

The QEMU compositor is a test backend, not a desktop simulation. It checks the
first kernel-owned frame, queues a press/release on the blue box, verifies that
real WebCore event dispatch executes the DOM's JavaScript and paints a green
second frame, queues close, and verifies exit 120 plus complete page reclamation.
The suite also exercises preemption, process isolation, threads, heap/VM bounds,
loading, cancellation and fault cleanup. The actual KWINDOW/KGFX adapter is built
and linked in an isolated desktop kernel; Hyper-V interaction still needs testing.

## Hyper-V package

The prepared package is `build/dihos-native-window-loading-20261101`. It preserves the
previous package's applications and resources, replaces boot/stage2/kernel with
the isolated build, and adds `UserRuntime/page.elf`, licences and source recipes.
The working checkout's kernel binary is retained. Package preparation mounts no
disk. To deploy, shut down DihOS and run in Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-native-window-loading-20261101
```

After boot:

```text
proc:run-ui 0:/OS/System/Programs/UserRuntime/page.elf
```

Click the blue box. The heading should become `clicked in DihOS` and the box
green. Move the window and use other desktop apps; close it and inspect
`proc:status`. Expected: exited, status `0x78`, native Page window success log.
`proc:stop` cancels outstanding native tasks and destroys their owned windows.

The diagnostic has a fixed 640 x 480 viewport. Desktop resizing clips or exposes
background around that fixed surface; it does not reflow the page. Keyboard/text
editing, cursor-shape feedback, live navigation/resource loading, damage-only
uploads, bitmap codecs, cookies and other browser platform services remain
integration work. Wheel routing is compiled but the click test does not validate
scrolling behavior. No modern-site or video compatibility is claimed here.

## Loading responsiveness update

The first Hyper-V report stopped in file-reading state 2 at 7.6 MiB; the
application had not entered userspace or created its window. The old loader
issued 64 KiB synchronous FatFs calls (up to sixteen StorVSC data requests)
from the desktop loop. It now releases the filesystem between 4 KiB reads,
checks a one-millisecond poll budget, and caps each poll at sixteen calls even
if the clock does not advance. One individual driver request remains synchronous
and can exceed that budget; this is recorded rather than hidden.

Image admission now batches its existing 64 KiB/256-relocation work units under
the runtime service's existing one-millisecond/64-step limits, instead of spending
one desktop frame per unit. Single-step polling remains available for cancellation
tests. `proc:status` reports readable KiB/percentage and disk read count/last/max
milliseconds, and explains when no user task exists because reading is incomplete.

The deployment ELF strips non-loadable symbols, reducing file reads from
126,969,128 to 102,928,232 bytes. `page.symbols.elf` retains debug lookup data;
the build proves identical loadable segments and runs the production validator
on the stripped file. It still needs to read about 98 MiB before the window opens.

Host tests cover slow disk calls, frozen-clock limits, partial reads, cancellation,
missing files, allocation/admission failure and grant routing. The revised ARM64
suite checks the actual stripped window executable and admission behavior; actual
Hyper-V latency still requires guest measurement. If it remains slow, send the new
`proc: disk read calls / last ms / max ms` line along with loading progress.
