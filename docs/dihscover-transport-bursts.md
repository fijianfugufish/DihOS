# Dihscover background transport recovery

The previous update exposed a kernel network timeout for Google while the
terminal request succeeded. This package addresses reproduced receive-path
defects; it does not claim a confirmed live-site fix before Hyper-V retesting.

The synthetic NIC previously queued 32 Ethernet frames while TCP advertised a
64,240-byte receive window. A yielding network worker can accumulate bursts
between polls. The queue now holds 128 frames and TCP advertises 16,384 bytes
without window scaling. A deterministic 64-frame burst drops frames with the
old driver and passes in order with the new driver; overflow remains bounded
and counted. A byte window alone does not bound the count of tiny segments.

Both HTTP and HTTPS now trim already received prefixes from retransmitted TCP
segments and deliver only their contiguous unseen suffix. Complete duplicates
and future segments are acknowledged without duplicate delivery. Sequence
wrap is covered. FIN is accepted only when it follows all received bytes;
a future FIN cannot terminate an incomplete stream. This is not a complete
TCP implementation: no out-of-order reassembly or new transmit-retry engine
is introduced here.

Kernel timeouts retain the active phase (router/DNS/TCP/TLS/HTTP), raw bytes,
TCP packet count and noncontiguous-segment count. The application worker now
preserves this specific error instead of overwriting it with a generic timeout.

Validation includes the production NetVSC parser and queue, production HTTP
and HTTPS transport loops with a modeled peer/TLS engine, actual ARM64
transport compilation, and real BearSSL certificate tests. The latter still
reject unknown roots, hostname mismatch, invalid dates, altered signatures
and malformed certificates. No private school CA has been installed.
The browser renderer/frontend are unchanged from the response-deadline package;
their sealed ARM64 rendering regression is inherited after source/hash checks.

Run host regressions with `tools/test_hyperv.ps1` and
`tools/test_user_http_headers.ps1`. `tools/tcp_receive_tests.c` additionally
tests sequence wrap and FIN positioning. Proof and packaging tools are
`tools/webcore_dihos/record_transport.py` and `package_transport.py`.
The isolated kernel snapshot excludes unrelated native graphics work and
does not replace the working-tree kernel binary.

With the VM off, run from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-transport-bursts-20261102
```

Open Dihscover normally and retry Google. If it still fails, retain the full
error line: the phase and counters now survive the kernel-to-browser boundary.
