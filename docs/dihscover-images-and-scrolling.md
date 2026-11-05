# Native images and interactive rendering

This milestone explicitly enables WebCore image loading and supplies its missing
DihOS bitmap decoder factory. SVG stays in WebCore's native SVG renderer; it does
not pass through the bitmap decoder. PNG, JPEG, GIF, BMP and WebP use the vendored
stb_image/simplewebp decoders inside the restricted renderer process. Cairo owns
premultiplied pixel storage even after the decoder is released. Limits are 8 MiB
encoded, 8192 per dimension, 16 million pixels and 64 MiB retained decoded pixels.
GIF currently displays its first frame; animated WebP is not guaranteed. AVIF is not supported.
Colour profiles and progressive raster display are later work.
The runtime also caps individual allocator requests at 8 MiB; large pixel or
temporary buffers can therefore fail gracefully below the overall decoder limits.

The frontend queues Google once on startup, through its existing asynchronous
navigation API. Stop does not trigger another startup navigation. Certificate
verification and the personal school CA are unchanged. A Google CAPTCHA or a
school block page is a server/network response, not a rendering failure.

Mouse movement no longer automatically causes full-viewport painting. WebCore
damage rectangles are clipped and merged, unchanged pixels skip upload, and
scroll distance is drained in steps of at most 80 pixels per rendered iteration.
Scroll and resize still require full viewport painting; changed frames still use
the existing atomic full-frame upload ABI. The trusted browser launcher assigns
its owned renderer a maximum 4 ms CPU service budget, keeping timer preemption;
other processes retain the 1 ms default. No new public syscall or permission is
added. Kernel service/cleanup time is outside this CPU quantum guarantee.

F12 reports active renderer CPU time separately from elapsed paint time, painted
pixels, frame/upload counts and queued scroll distance. Hyper-V and native
hardware FPS and latency still require guest measurement; no FPS target is
claimed from QEMU results.

Validation uses the real ARM64 WebCore/JSC/Cairo image pipeline and production
EL0/SVC/VM-copy/window broker under QEMU. Network delivery is modeled. It checks
actual pixels for inline SVG, data-URL SVG, external SVG and each raster format,
plus links, scroll, fixed elements, resizing, history, error recovery and cleanup.
Additional decoder checks cover alpha premultiplication, retained surface
ownership, truncated data and encoded size rejection. Host checks cover damage
bounds, wheel accumulation, frontend startup, ABI compatibility and lifecycle.

From WSL Ubuntu, with the existing verified engine archives and toolchain:

```sh
python3 tools/webcore_dihos/build_images_perf.py
python3 tools/webcore_dihos/test_images_perf.py --include-page
```

Build the frontend and isolated kernel in PowerShell (the personal CA header is
an optional local build input; never commit the recovered certificate):

```powershell
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -OutputDirectory build/dihscover-images-perf-validation/frontend -EngineDirectory build/dihscover-images-perf-validation/renderer
python tools/export_user_snapshot.py --output build/imgperf/src --extra-ca-header build/dihscover-school-ca-validation/qe-ca.h
.\build\imgperf\src\build.ps1 -ProjectRoot .\build\imgperf\src -ArmOnly
```

The validation package combines those exact binaries with the previous asset
manifest, preserves existing licences and adds the decoder notices. Build/source
hashes and inherited test evidence accompany it. With the VM turned off, install
from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-images-perf-20261103
```

Guest checks: let Google start without pressing Stop; try Wikipedia images and
SVGs, scroll a long page, move across links, resize, and compare F12 CPU/wall
figures. This package does not bypass challenges or school filtering.
