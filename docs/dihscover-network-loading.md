# Dihscover resource loading and error recovery

The isolated WebCore browser now accepts responses up to 8 MiB instead of
64 KiB. Each process still has four kernel request slots and checked 4 KiB
reads. The broker reserves at most 32 MiB of response storage per process
(128 MiB across all 16 slots); allocation failure remains a reported error.
This is a larger bounded cumulative buffer, not unlimited streaming storage.
The raw transport allowance includes response headers and chunk framing.

When slots are busy, the owner RunLoop retains URL/header copies and retries
on its 10 ms poll timer. There are at most 64 waiting loads. Waiting consumes
the original request deadline; it never resets a timeout. Cancellation and
closing discard waiting copies without accessing nonexistent kernel handles.
The existing core-0 network continuation remains the only transport owner.

A failed document navigation displays a local WebCore HTML error page with
the failed URL, detailed reason, error code and Try again link. Reload retries
the failed URL. Error text and attributes are escaped, retry targets are
restricted to HTTP/HTTPS, and the page needs no external resources or scripts.
No certificate checks are bypassed. Subresource errors do not replace an
otherwise useful document with an error page.

Validation covers native MiB-sized bodies, malformed sizes, request-slot
backpressure, owned queued URLs, deadlines, cancellation, error-page escaping,
real ARM64 error-page painting and retry, plus previous scrolling/resize/input
regressions. QEMU uses a modeled transport, not live Internet networking.
The host Google probe returned 86,776 bytes with identity encoding: larger
than the previous response bound. Other host site probes encountered TLS
timeout or local trust failures; those results do not diagnose the guest.

This does not establish compatibility with every Google or DuckDuckGo page.
The port still supports GET only, has cookies disabled, incomplete bitmap
decoding and no media playback. A POST-based search form now reports the
unsupported operation instead of silently leaving a blank page. The existing
toolbar search continues to use DuckDuckGo's HTML GET endpoint.

Build and verification:

```powershell
.\tools\test_user_http_headers.ps1 -Output build/dihscover-network-validation/http
.\tools\test_dihscover_integration.ps1 -Output build/dihscover-network-validation/host
.\tools\test_browser_controls.ps1 -Output build/dihscover-network-validation/window-host
```

Native builds use the pinned WSL WebCore configuration and the commands in
`docs/dihscover-webcore-integration.md`. Milestone proof and package tools are
`tools/webcore_dihos/record_network.py` and `package_network.py`. They require
the tested renderer, isolated kernel, normal frontend and retained test logs.

With the VM off, run from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-network-loading-20261102
```

Open Dihscover normally. Guest validation of the affected live sites remains
necessary; the package tool does not mount or update a VHD.
