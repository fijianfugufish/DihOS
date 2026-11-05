# Dihscover software view-transition fallback

The watch-page report exited with status 180 (`0xB4`) and
`Page: unexpected accelerated graphics layer`. This is an explicit renderer
exit, distinct from the previous allocation failure.

In pinned WebCore, `Document::startViewTransition()` calls
`setActiveViewTransition()`, which eagerly enables compositing even when the
page disables accelerated compositing. The captured YouTube client uses this
API around navigation updates. A native control fixture must reproduce the
same graphics-layer exit with the original Document implementation.

The DihOS override skips a visual transition before activating compositing when
the software backend is selected. It uses WebCore's existing skip algorithm:
the DOM update callback still runs asynchronously, `ready` rejects with
`AbortError`, and `updateCallbackDone` and `finished` settle from the callback's
actual result. Cross-document visual transitions opt out before snapshotting.
Ordinary DOM updates, rendering, JavaScript and navigation remain available.
The unsupported GraphicsLayer factory still fails explicitly; no invisible
placeholder layers or GPU capability claims are introduced.

The override compiles the complete matching Document unity unit with the pinned
engine's exact flags and headers. The original engine source and archives stay
unchanged. The build records upstream, patched-source, compiler and object hashes.

Native tests exercise asynchronous callbacks, successful/rejected/thrown updates,
promise settlement, repeated/nested transitions, explicit repeated skips, real
Cairo pixels, cross-document opt-out and process cleanup. Browser/navigation,
network and MediaSource regressions supplement this fixture. Live YouTube
playback still requires a guest test.

From Ubuntu WSL in the repository (one compiler job at a time):

```sh
python3 tools/webcore_dihos/test_damage.py --output build/dihscover-transitions-validation --software-view-transitions
```

From PowerShell:

```powershell
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -EngineDirectory build/dihscover-transitions-validation/engine -OutputDirectory build/dihscover-transitions-validation/frontend
python tools/webcore_dihos/package_optional_trace.py --transitions
python tools/webcore_dihos/package_optional_trace.py --transitions --check
```

With the VM off:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-software-transitions-20261105
```

This package retains the prior kernel, 768 MiB trusted browser budget, allocator
pressure handling, assets and partial surface updates. Packaging does not modify
the VHD.
