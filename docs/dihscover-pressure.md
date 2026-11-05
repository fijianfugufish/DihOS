# Dihscover watch-page memory pressure

The reported watch-page fault maps to `WTF::fastCompactMalloc` in the matching
renderer. The last allocation was `0xa46bab` bytes (about 10.3 MiB), allocator
reason 3 (backing/quota failure), with 20 pools, `0x181c0000` bytes reserved
and `0xe7a1480` bytes live. Peak committed heap was `0x18b59000` bytes.
The renderer image alone occupies 111,140,864 bytes; image, thread-stack and
page-table allowances also count toward the previous 512 MiB process cap.
The screenshot supports memory pressure; it does not distinguish every possible
physical allocation failure from quota exhaustion.

On backing allocation failure, the allocator now returns wholly idle secondary
pools to the kernel before retrying. Live objects and the TLSF control pool
stay in place. Warm reuse remains available when there is no pressure. A fixed
quota regression reproduces failure with the sealed prior allocator and succeeds
with reclamation, checking live contents and complete cleanup in both cases.

The kernel-only trusted browser launcher grants a bounded 768 MiB total process
budget. Ordinary apps, including those granted networking and a window, retain
their 256 MiB budget. There is no automatic escalation by capability or syscall.
The 64 MiB owned-region ceiling, pool slots and page-table limits stay bounded.
Large allocation failures report request size, allocator usage, reason, private
bytes, committed heap and active heap limit in an allocation-free log snapshot.

This retains the prior partial-upload and redraw-coalescing improvements. Local
ARM64 browser/media tests do not establish that live YouTube playback works.

Build and test from Ubuntu WSL in the repository:

```sh
python3 tools/webcore_dihos/test_pressure.py
```

Build the isolated kernel and existing frontend, then seal from PowerShell:

```powershell
python tools/webcore_dihos/prepare_youtube_kernel.py --pressure
.\build\youtube-pressure-kernel\src\build.ps1 -ProjectRoot .\build\youtube-pressure-kernel\src -ArmOnly -KernelOnly
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -EngineDirectory build/dihscover-pressure-validation/engine -OutputDirectory build/dihscover-pressure-validation/frontend
python tools/webcore_dihos/package_optional_trace.py --pressure
python tools/webcore_dihos/package_optional_trace.py --pressure --check
```

With the VM off:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-pressure-20261105
```

Retest the same search and watch-page navigation. If it faults again, capture
`proc:status`; the allocation snapshot now contains the limit and usage together.
No VHD modification is performed by the test or packaging commands.
