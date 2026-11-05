# Dihscover bounded damage updates

Small painted areas (at most 16,384 pixels) use an append-only, capability-gated
SACX surface-region transaction. The kernel validates ownership, bounds and all
source pages before admitting a write. Incomplete transactions remain invisible;
presentation updates only the declared region. Full-frame callers keep their
existing ABI, and the browser falls back to full uploads on older kernels.

A 32 x 32 repaint now transfers 4 KiB in one pixel-upload call, instead of
approximately 7.9 MiB in 127 calls at 1920 x 1080. This is transfer accounting,
not a measured Hyper-V frame-rate claim. The desktop compositor still receives
an image invalidation; this does not implement compositor damage rectangles.
Redraws coalesce at a minimum 16 ms interval while commands continue to drain.
Scrolling still repaints the viewport, preserving fixed and sticky content.

F12 reports pending request method, response phase, status and received/delivered
byte counts in the browser status area and terminal. These pending-request
snapshots omit query strings, request headers, cookies and bodies. This is intended to diagnose the live
YouTube consent spinner; a complete local fixture does not establish that the
live consent service succeeds.

The ARM64 fixture exercises a DOM cookie, bodyless POST, a credentialed cross-origin
POST returning 204, and reading the first response after the second completes.
TLS, CORS, cookie scoping and user consent are not bypassed.

From Ubuntu WSL in the repository:

```sh
python3 tools/webcore_dihos/test_damage.py
```

From PowerShell, build the isolated kernel and frontend, then seal the package:

```powershell
python tools/webcore_dihos/prepare_youtube_kernel.py --damage
.\build\youtube-damage-kernel\src\build.ps1 -ProjectRoot .\build\youtube-damage-kernel\src -ArmOnly -KernelOnly
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -EngineDirectory build/dihscover-damage-validation/engine -OutputDirectory build/dihscover-damage-validation/frontend
python tools/webcore_dihos/package_optional_trace.py --damage
python tools/webcore_dihos/package_optional_trace.py --damage --check
```

With the VM off, deploy the sealed package:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-damage-20261105
```

Live acceptance remains: compare scrolling and animation latency with the prior
package; retry YouTube's chosen consent action and use F12 while it is pending.
No live YouTube playback or Hyper-V performance result is claimed by host tests.
