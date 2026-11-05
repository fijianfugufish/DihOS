# Native WebCore software graphics bring-up

The experimental WebCore port uses Cairo 1.18.6 and Pixman 0.46.4 compiled
for `aarch64-none-elf` against DihOS's userspace ABI. It does not call a host
renderer. The current image-only profile has no FreeType, fontconfig, GPU,
window-system or filesystem backend. Font shaping, WebCore Page/Frame hookup
and presenting document surfaces are separate outstanding work.

[Recorded validation](browser-evaluation/native-cairo-20261031.json): the
45-group ARM64 regression suite passed. After the final bilinear-only stack
padding and scaled-image test were added, the focused 30-group runtime and
graphics suite also passed against the final libraries. Both normal raster
completion (113) and controlled critical OOM (178) reclaim every process page.
The four WebCore units listed below compile. Graphics execution has not yet
been validated on Hyper-V or physical hardware.

`tools/webcore_dihos/build_graphics.py` verifies the source archives against
the publisher's pinned checksums, restricts dependency discovery to the
native staging directory, and records library and ABI source hashes in
`build/webcore-graphics/report.json`. A separate Cairo configuration leaves
the resource-only WebCore build intact.

Native adaptations preserve real functionality:

- Cairo mutexes use process-local SACX thread identity and checked wait/wake
  calls, with recursive ownership and misuse detection.
- Cairo's jump buffers use pinned musl AArch64 assembly and a private ABI.
- Pixman cleanup registers with the tested userspace `atexit` registry;
  unsupported ELF finalizer tags are not accepted by weakening the loader.
- Three Pixman NEON frames are padded from 232 to 240, 88 to 96 and 120 to 128 bytes.
  Saved-register offsets remain unchanged. This preserves 16-byte stack
  alignment when DihOS preempts and resumes drawing.
- Cairo temporary-file creation is denied. The native C locale is used.
- Cairo's internal clip constructor has callers that dereference a failed
  allocation without checking. That unrecoverable allocation exits only the
  owning process with status 178 and an explicit diagnostic. It does not
  silently drop clipping or continue with a null pointer. Other tested
  recoverable allocations retain Cairo's ordinary error/status handling.

Run from the checkout in PowerShell, with the native prerequisites already
built as described in the JavaScriptCore and resource-client documentation:

```powershell
wsl.exe -d Ubuntu --cd $PWD.Path -- python3 tools/webcore_dihos/build_graphics.py
wsl.exe -d Ubuntu --cd $PWD.Path -- python3 tools/webcore_dihos/build_raster_fixture.py
wsl.exe -d Ubuntu --cd $PWD.Path -- python3 tools/webcore_dihos/build_raster_fixture.py --critical-oom
.\tools\test_user_runtime_qemu.ps1 -CairoRaster -ReusePrograms
```

`-ReusePrograms` verifies the previously built base fixture hashes. Omit it
when those fixtures need rebuilding. Pass `-Python` if Python is not on PATH.
The raster executable and critical-OOM executable must pass the production
ELF validator and have zero unresolved imports before the harness embeds them.
Neither is given a network capability.

The raster fixture checks image guard bytes and exact fill/clip/alpha/gradient
pixels, transformed paths, bilinear image scaling, invalid dimensions, native jumps, recursive mutex
contention, independent surfaces on three workers, recoverable allocation
failure points 0–3, and drawing after recovery. The second fixture sweeps
allocation failures until it reaches the critical clip allocation path.
Its exact index depends on cache and allocator state. The kernel harness checks the
expected exits (113 and 178), bounded service calls and complete process-page
reclamation. These are raster-backend checks, not proof of HTML rendering or
JavaScript website compatibility.

For compile diagnostics of the real WebCore backend:

```powershell
wsl.exe -d Ubuntu --cd $PWD.Path -- python3 tools/webcore_dihos/configure_native.py --graphics
wsl.exe -d Ubuntu --cd $PWD.Path -- python3 tools/webcore_dihos/compile_units.py --report build/webcore-cairo/report.json --unit dom/Document.cpp --unit html/parser/HTMLDocumentParser.cpp --unit rendering/RenderView.cpp --unit platform/graphics/cairo/GraphicsContextCairo.cpp
```

Document, the parser, RenderView and GraphicsContextCairo compile. Document's
cache headers use upstream's generic reference-counted wrapper ownership on
DihOS and the real SACX-backed `getCurrentProcessID()` for diagnostics. This
does not provide a system accessibility bridge. A successful Document object
compile does not establish a complete WebCore link or local page execution.
The shipping NetSurf executable, installed VHDX and working-tree kernel have
not been replaced by these experiments.
