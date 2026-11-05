# Progress-aware browser response timeouts

The guest diagnostic reported a network response timeout after receiving and
delivering 6,549,316 bytes of YouTube's startup script, with no response
deferral. The browser still imposed its original fixed 30-second deadline on
the whole transfer. Raising the size allowance alone did not remove that
deadline.

The browser now renews its response idle deadline only when it observes more
received bytes or the first response headers. Polling, queued callbacks and
delivery of already-received bytes do not renew it. Default idle timeout is
30 seconds. A separate 120-second hard total deadline bounds an active
request, including trickling responses. That same total budget is passed to
the existing kernel broker, whose TLS/DNS/TCP and cancellation checks remain
enabled. Completed responses retain their separate bounded consumer watchdog.

The 16 MiB response allowance, checked 4 KiB copies and existing media limits
are retained. The kernel and boot files are unchanged from the previous
YouTube-startup package. No TLS verification or origin checks are disabled.

Native ARM64 WebCore fixtures model a transfer which continues for 45 seconds,
a stalled transfer which exceeds the idle deadline, and an endless trickle
which reaches the hard total limit. They check completion/failure and one-time
request cleanup. The existing response-size, browser, loader and MediaSource
playback regressions run against the same renderer. Live YouTube and Shorts
still require guest validation.

Build and test from ARM64 Ubuntu WSL:

```sh
python3 tools/webcore_dihos/media/build_renderer.py --mse --output build/dihscover-youtube-progress-validation --resume
python3 tools/webcore_dihos/media/test_mse_page.py --output build/dihscover-youtube-progress-validation
python3 tools/webcore_dihos/media/test_mse_regressions.py --output build/dihscover-youtube-progress-validation
python3 tools/webcore_dihos/media/prepare_mse_frontend.py --output build/dihscover-youtube-progress-validation
```

Then in Windows PowerShell:

```powershell
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -EngineDirectory build/dihscover-youtube-progress-validation/engine -OutputDirectory build/dihscover-youtube-progress-validation/frontend
python tools/webcore_dihos/package_youtube_bootstrap.py --progress
python tools/webcore_dihos/package_youtube_bootstrap.py --progress --check
```

With the VM off, install the package from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-youtube-progress-20261104
```

If YouTube remains on placeholders, F12 retains the error in the status bar
and `proc:status`. Initial script loading may still take time; this change
prevents active transfers being cut off by the old total deadline.
