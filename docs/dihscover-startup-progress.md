# Browser startup and Stop

Dihscover opens its small toolbar frontend before reading the approximately
100 MiB WebCore executable. The previous Loading indicator covered both this
disk read and the later webpage load. A Google spinner alone did not mean
Google had started: `proc:status` state 2 with no user task means the executable
is still being read. The reported Google page did finish after waiting.

The frontend now displays executable-read KiB and percentage while starting.
Stop during this stage closes the owned session, cancelling the pending file
load and releasing its staging pages. Reload or Go creates a fresh session.
After startup, Stop first asks WebCore to cancel normally. Stale Loading reports
are suppressed; if cancellation remains unacknowledged after one second, the
frontend closes that renderer. This fallback discards its page, history and
session cookies; Reload starts it again. Acknowledged Stop retains the page.

Google remains the initial page and default search. The renderer, TLS, cookies,
fonts, image codecs and one-ms/4-KiB loader budget are unchanged. This update
does not claim to accelerate the executable read. Browser-session operation 8
is an optional frontend-only progress query; existing command/info structures
and renderer operations retain their layouts.

Run `tools/test_dihscover_integration.ps1 -Output build/dihscover-startup-validation/host`
for the actual frontend/broker lifecycle, progress ownership, immediate startup
cancel, stale status, graceful cancellation, bounded fallback and reopen tests.
The isolated kernel is prepared by `tools/webcore_dihos/prepare_startup_snapshot.py`
from the previous selected source snapshot plus only the broker/header changes.
Compile `build/stp/src/build.ps1 -ProjectRoot build/stp/src -ArmOnly`, then build
the frontend with `sdk/sacx/apps/dihscover/build_webcore.ps1 -OutputDirectory
build/dihscover-startup-validation/frontend -EngineDirectory
build/dihscover-cookie-validation/renderer`.

With the VM off, use Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-startup-progress-20261103
```

Guest checks: launch and observe Starting browser/read percentage; press X
during startup, then Reload; wait for Google to finish; navigate and use Stop.
The new progress/cancel UI still requires this guest test. Executable reading
remains synchronous in bounded chunks, so an individual slow disk request can
still exceed the normal polling budget.
