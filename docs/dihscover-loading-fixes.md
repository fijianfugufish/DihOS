# Dihscover loading fixes — 3 November 2026

Google redirects can contain long query parameters. The previous userspace
transport rejected URLs longer than 767 bytes, while the kernel transport could
silently cut the request path at 511 bytes. Requests, copied URLs, redirect
locations and HTTP request construction now share an 8,191-byte limit. Invalid
or oversized input fails before an incomplete request is sent. Existing public
response-info structures retain their ABI and presentation-field sizes.

Wikipedia's redirect response contains a valid empty `x-analytics` header.
The adapter incorrectly required conversion through a UTF-8 parser intended
for nonempty values. It now extracts bounded HTTP fields and maps values to
WebCore as 8-bit strings, including empty values and Latin-1 bytes. Header names,
line endings, controls, terminators and bounds are still checked. Failures now
identify the response-header phase instead of only reporting a generic error.

The startup address is Google; ordinary queries already use Google. Explicit
URLs retain their original destination. The existing toolbar, fonts, compositor,
window/input behavior and certificate policy remain in place. The personal
school CA is the same explicitly selected certificate as the previous package.
DuckDuckGo's school filtering rule remains independent of certificate trust.

## Validation

- Broker tests preserve every byte of an 8,191-byte owned URL, reject protected
  and oversized buffers, and retain per-process ownership and cancellation.
- The actual guest HTTPS/TCP loop sends the final query parameters across
  multiple packets without truncation. TLS is mocked in this transport test.
- Header tests cover empty/whitespace-only values, Latin-1, repeated fields,
  malformed names, controls, folded lines and incomplete/trailing framing.
- The actual WebCore resource adapter executes in ARM64 EL0, including empty
  and Latin-1 fields, redirects, cancellation and rejection of unverified TLS.
  Network delivery in that kernel is modeled, not a live NIC test.
- The freestanding TLS/HTTP host probe follows Google search redirects through
  consent.google.com to a complete authenticated 200 response, and Wikipedia's
  root redirect to its Main Page. This tests the same decoding code over host
  sockets; it does not prove guest network delivery or full live-site layout.
- The desktop ARM64 kernel builds from a selected snapshot. The existing engine
  archives are verified and linked with explicitly rebuilt network adapters and
  client objects. Earlier source trees, archives, packages and proofs are kept.

The renderer and focused resource fixture are checked by the production ELF
loader. The ARM64 execution report records the actual fixture hashes and groups
that ran. Live Hyper-V retesting and physical hardware testing remain necessary.

## Build and test

Use a short snapshot path to avoid Windows path-length limits:

```powershell
python tools/export_user_snapshot.py --output build/loadfix/src --extra-ca-header build/dihscover-school-ca-validation/qe-ca.h
.\build\loadfix\src\build.ps1 -ProjectRoot .\build\loadfix\src -ArmOnly
.\tools\test_user_http_headers.ps1 -Output build/my-loading-tests/http
.\tools\test_hyperv.ps1
```

In ARM64 Ubuntu WSL, with the previously validated engine configuration:

```sh
python3 tools/webcore_dihos/build_loading_fix.py
python3 tools/webcore_dihos/test_loading_fix.py --include-page
```

The build helper verifies baseline archive hashes and uses isolated aliases for
unchanged SDK/platform headers. The three changed networking implementation
objects resolve before the static engine archives. Upstream engine code and
feature configuration are unchanged; all rendering still executes inside DihOS.

Build the normal frontend against that renderer:

```powershell
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -OutputDirectory build/dihscover-loading-fix-validation/frontend -EngineDirectory build/dihscover-loading-fix-validation/renderer
```

With the VM off, install from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-loading-fix-20261103
```

Open Dihscover normally and test a Google query and Wikipedia. Source archives,
build identifiers, hashes and test reports accompany the packaged binaries.
