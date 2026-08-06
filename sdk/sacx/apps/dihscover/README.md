# Dihscover

Dihscover is the SACX browser shell. It uses the append-only SACX network ABI
and bounds every document, image, history, and paint allocation. Network jobs
currently execute on the main core because the xHCI controller state is global;
the ABI remains asynchronous so a cooperative main-core transport can replace
that compatibility path without changing applications.

## Build

The repository build does not build Dihscover automatically. Run this explicitly:

```powershell
.\sdk\sacx\apps\dihscover\build.ps1 -ProjectRoot . -BuildX64
```

This writes `build\sacx\Dihscover.sacx`. The script delegates to
`build_sacx_app.ps1`, which selects `clang` for C, `clang++` for C++, and uses
linker response files.

## Engine boundaries

- `main.cpp`: browser chrome, history, navigation generations, resources, paint pool.
- `document.cpp`: dynamically growing bounded DOM, HTML recovery, stylesheet cascade,
  inheritance, block flow, and a flex-row subset.
- `url.cpp`: URL input normalization and relative reference resolution.
- `script.cpp`: pointer-free JavaScript compatibility bridge for DOM text/style
  mutations, click handlers, selectors, and bounded timers.
- `runtime.cpp`: freestanding C runtime primitives.
- `heap.cpp`: TLSF allocator over a 16 MB task-owned SACX memory allocation.

Pinned upstream sources live in `third_party/quickjs-ng` and
`third_party/lexbor`, with TLSF in `third_party/tlsf` and SimpleWebP in
`third_party/simplewebp`. QuickJS-NG is pinned to v0.15.0. Lexbor has no upstream
v2.9.0 tag, so the nearest official compatible release, v3.0.0, is pinned and
its license is preserved.
Dihscover grows to 32,768 DOM nodes and 8 MB of document strings within its heap.
Pages that exceed those limits render a safe truncated document instead of
growing kernel-facing graphics state. Images are loaded near the viewport and
decoded from PNG, JPEG, BMP, WebP, or the initial bounded SVG subset.
