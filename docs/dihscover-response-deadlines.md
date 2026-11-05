# Dihscover response deadlines

The browser previously applied its request-start deadline to delivery callbacks
as well as network I/O. An already completed download could therefore become
a network timeout while WebCore paused response policy or processed body data.

The resource client now polls the transport before deciding that a request
timed out, including while the client has deferred delivery. The original
deadline still bounds admission and incomplete transport work. Completed
responses instead get a separate 120-second consumer watchdog. This does not
extend the kernel's DNS/TCP/TLS deadline or disable TLS verification.

Timeout errors report the phase, received bytes, delivered bytes and whether
delivery was deferred. This distinguishes request-slot exhaustion, the kernel
network queue, an incomplete response and a stalled browser consumer. An
existing transport error retains its more specific message.

ARM64 resource tests pause response policy beyond the original deadline and
consume a completed body slowly; both must finish successfully. A permanently
paused consumer must hit its separate watchdog and release its handle. The
previous network deadline, cancellation, size and ownership checks remain.
The normal embedded browser regression also runs against the updated client.

Only renderer sources change. The kernel, transport, roots, frontend and assets
come from the verified network-loading package. Host kernel/UI tests are
inherited only after checking their tested source hashes are unchanged.
Live Google browsing in Hyper-V still requires a guest test; these regressions
prove the deadline correction, not the cause of every reported site failure.

The observed DuckDuckGo error 62 is separate: the host received a certificate
chain issued by the school HTTPS inspection authority, which is absent from
the public CA bundle. No private CA has been installed or implicitly trusted.

With the VM off, update from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-response-deadline-20261102
```

Open Dihscover normally and retry Google. If it still fails, the error page now
provides the phase and byte counts needed to locate the remaining stall.
