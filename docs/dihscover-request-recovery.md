# Request sizes, disk reads and renderer recovery

The screenshot's error concerned outgoing HTTP headers or a submitted form,
not the downloaded page. Responses already reach WebCore in chunks; the broker
currently retains a bounded 8 MiB response per request. Streaming delivery does
not remove the need for memory limits. This update raises request headers from
2 KiB to 16 KiB and memory-only POST bodies from 4 KiB to 16 KiB, consistently
across the SDK, validator, owned kernel slots, wire buffer and native adapter.
At most 128 request header fields are accepted. Protected-buffer checks,
framing restrictions, TLS verification and four owned request slots remain.
File/blob uploads and streaming large request uploads remain unsupported.

Browser executable reads now request up to 16 KiB each within the existing
four-millisecond polling budget, capped at 16 calls/256 KiB per poll. Other
apps retain 4 KiB reads and a one-millisecond budget. A synchronous storage call
can exceed the budget; elapsed time is checked between calls. Guest startup
time and input latency need validation; modeled timings are not VM timings.

The reported stopped renderer exited with code 180/0xB4 and the diagnostic
`Page: unexpected accelerated graphics layer`. The Cairo software port inherited
WebCore's default accelerated-compositing setting. The page now explicitly
disables accelerated compositing and accelerated fixed-position compositing
before frame initialization. The graphics-layer diagnostic remains fail-closed
for genuinely unsupported paths; it is not replaced with a fake layer. A native
fixture paints transform/`will-change` CSS through software and exercises clicks.

The frontend also closes dead renderer sessions. Reload or entering a new
address creates a new renderer rather than queueing commands to the dead one.
Ordinary page-load failures retain the renderer and error page. Restart loses
that renderer's session cookies and history. The kernel reports a faulted
renderer as exited, while `proc:status` retains fault/exit diagnostics.

Validation uses the actual broker/frontend lifecycle tests, owned 16 KiB header
and body copies, protected buffers, oversize rejection, request parser corpus,
native ARM64 WebCore queue retry and software transform painting. QEMU models
network delivery; live Google verification and guest performance remain pending.

Rebuild with `prepare_request_snapshot.py`, then the selected
`build/rr/src/build.ps1 -ProjectRoot build/rr/src -ArmOnly`. Build the renderer
in WSL with `python3 tools/webcore_dihos/build_request_limits.py`; run
`python3 tools/webcore_dihos/test_request_recovery.py --include-page`. Build
the frontend using `build_webcore.ps1 -OutputDirectory
build/dihscover-request-recovery-validation/frontend -EngineDirectory
build/dihscover-request-recovery-validation/renderer`. Host checks use
`tools/test_dihscover_integration.ps1` and `tools/sacx_network_service_tests.cpp`.
The source archive and proof retain selected kernel inputs, personal CA,
renderer hashes and test reports without modifying the working kernel.

With the VM off, run Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-request-recovery-20261103
```

Time a fresh launch, repeat the Google search/form, check the page which exited,
and exercise Stop/Reload. If a renderer still stops, send its last SACX log and
`proc:status` exit status rather than assuming the cause is the same.
