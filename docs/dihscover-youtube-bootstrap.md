# YouTube startup response allowance

The guest rendered YouTube's loading placeholders and then stayed idle. Its
process remained alive and reported the document finished. A public homepage
capture on 4 November 2026 referenced a 10,775,508-byte startup JavaScript
resource, exceeding both the renderer and EL0 network broker's 8 MiB response
limits. This is a concrete compatibility blocker; it does not establish that
every remaining YouTube requirement is implemented.

The shared response allowance is now 16 MiB. Four owned network slots bound
each process's broker buffers to 64 MiB. Checked reads stay at 4 KiB, upload
bodies and request headers stay at 16 KiB, and media segment/decoder limits
remain independent. Oversized responses report a size error. No TLS, origin,
cookie or cross-origin checks are disabled.

The renderer retains bounded script error/warning and failed-request
snapshots, including a source or URL. F12 reports counts, logs the retained
error for `proc:status`, and displays its summary in the existing status bar.
Navigation clears the snapshots. No headers, cookies or response bodies are
stored in these diagnostics.

Host tests check response ownership, quota, cancellation, protected reads
beyond 8 MiB, and diagnostic capture under address/undefined-behavior
sanitizers. Native ARM64 WebCore tests deliver 12 MiB and exact 16 MiB responses
and reject one byte beyond the limit. The existing rendering/network/browser
regressions and MediaSource playback fixture run against the same renderer.
These tests do not substitute for live YouTube/Shorts validation in the guest.

Build the isolated kernel in Windows PowerShell:

```powershell
python tools/webcore_dihos/prepare_youtube_kernel.py
.\build\youtube-kernel\src\build.ps1 -ProjectRoot .\build\youtube-kernel\src -ArmOnly
.\tools\test_user_http_headers.ps1 -Output build/dihscover-youtube-bootstrap-validation/http-tests
```

Run the renderer build and native tests in ARM64 Ubuntu WSL:

```sh
python3 tools/webcore_dihos/test_browser_diagnostics.py
python3 tools/webcore_dihos/media/build_renderer.py --mse --output build/dihscover-youtube-bootstrap-validation --resume
python3 tools/webcore_dihos/media/test_mse_page.py --output build/dihscover-youtube-bootstrap-validation
python3 tools/webcore_dihos/media/test_mse_regressions.py --output build/dihscover-youtube-bootstrap-validation
python3 tools/webcore_dihos/media/prepare_mse_frontend.py --output build/dihscover-youtube-bootstrap-validation
```

Then in Windows PowerShell:

```powershell
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -EngineDirectory build/dihscover-youtube-bootstrap-validation/engine -OutputDirectory build/dihscover-youtube-bootstrap-validation/frontend
python tools/webcore_dihos/package_youtube_bootstrap.py
python tools/webcore_dihos/package_youtube_bootstrap.py --check
```

With the VM turned off, Administrator PowerShell can install the matched
kernel and renderer while preserving user files:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-youtube-bootstrap-20261104
```

The previous sealed package and working-tree kernel binary are preserved.
