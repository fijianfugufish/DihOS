# Startup spacing and browser read budget

The status line now reserves `text_line_height(1, 0)` pixels, followed by a
six-pixel gap before the four-pixel progress bar and an eight-pixel gap before
the webpage. This replaces a hard-coded 17-pixel text area which intersected
the larger system font. Layout recalculates the spacing when the font changes.

WebCore is 103,968,664 bytes after stripping; almost all of those bytes are in
loadable segments. Loading it is real executable I/O, not a Google request.
The prior loader allowed only one millisecond of reading per desktop frame,
then deferred further reading until the next frame. Frame pacing compounded
the cost of reading a large executable through many small Hyper-V disk calls.

Owned browser launches now allow four milliseconds and at most 64 reads per
poll. Other ELF launches retain one millisecond and at most 16 reads. Every
request remains at most 4 KiB; elapsed time is checked after each read and a
slow individual request causes an immediate return. Cancellation, page staging,
image validation, grants and renderer CPU scheduling are unchanged. These are
polling budgets, not hard bounds on synchronous driver latency.

The modeled 100 MiB test with one-millisecond reads used 6,402 browser polls
versus 25,602 ordinary polls, including setup/admission. It also checks stalled
clocks, oversized time per read, and cancellation. This is not a Hyper-V wall
time benchmark; actual launch time and UI latency still need a guest test.

Build and validation:

```powershell
.\tools\test_dihscover_integration.ps1 -Output build/dihscover-startup-speed-validation/host
python tools/webcore_dihos/build_startup_budget.py
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -OutputDirectory build/dihscover-startup-speed-validation/frontend -EngineDirectory build/dihscover-cookie-validation/renderer
python tools/webcore_dihos/package_startup_speed.py
python tools/webcore_dihos/package_startup_speed.py --check
```

The kernel build first relinks the preceding selected objects unchanged and
requires an exact SHA-256 match to the validated kernel. It then recompiles
only the loader using the original build script's flags and headers, links a
new kernel and verifies all reused objects stayed unchanged. Its source archive
contains the updated loader, original source manifest and base commit. The
previous kernel, personal CA, unrelated native changes and working checkout
kernel remain intact. The cookie renderer is reused byte-for-byte.

With the VM off, run from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-startup-speed-20261103
```

Check status/bar separation, time a fresh launch, use the desktop while loading,
and cancel/reload during startup. Guest timing and native hardware testing are
pending. The browser still needs to read the full engine on each new launch.
