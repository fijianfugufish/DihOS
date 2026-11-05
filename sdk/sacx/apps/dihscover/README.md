# Dihscover

Dihscover retains its existing SACX browser chrome in `main.cpp`. The default
ARM64 build uses `webcore_backend.cpp` to connect those controls to the isolated
WebCore renderer inside Dihscover's application bundle. See
`docs/dihscover-webcore-integration.md` for build, deployment and validation.

The NetSurf 3.11 backend remains available with `build.ps1 -Engine NetSurf`.
With that backend, NetSurf owns HTML parsing, the DOM, CSS selection, layout,
history, form handling, resource discovery and page painting. The backend
interface retains its original names for source compatibility.

## DihOS front end

- `main.cpp`: native SACX window, address bar, navigation controls, and input.
- `webcore_backend.cpp`: existing UI callbacks connected to an isolated renderer.
- `netsurf_backend.cpp`: NetSurf GUI operation tables, scheduler, bitmap store,
  font measurement, ARGB plotter, and the single composited page surface.
- `netsurf_fetch.cpp`: asynchronous `http` and `https` fetcher backed by the
  append-only SACX network ABI. No libcurl or POSIX socket layer is used.
- `runtime.cpp`, `heap.cpp`: freestanding runtime and a bounded 64 MB TLSF heap.

The page surface is a task-owned ARGB image obtained through `img_create` and
`img_pixels`. NetSurf renders into that surface; `img_touch` publishes a
completed frame to the normal SACX graphics compositor.

## Legacy renderer

The original hand-written parser, DOM, CSS/layout engine, script bridge, URL
resolver, and shell are preserved under `legacy/`. They are intentionally not
listed in `build.ps1` and are not compiled.

## Build

```powershell
.\sdk\sacx\apps\dihscover\build.ps1 -ProjectRoot . -BuildX64
```

This writes `build\sacx\Dihscover.sacx`. NetSurf and its libraries are compiled
directly into the application from `third_party\netsurf-all-3.11`.

## Licensing

NetSurf is GPL-2.0. The active Dihscover binary links NetSurf directly and must
therefore be distributed in accordance with GPL-2.0. NetSurf's `COPYING` file
and the licenses for every bundled NetSurf library are preserved in the
vendored source tree.

