# Dihscover session cookies and verification compatibility

The normal Dihscover app now has a single process-owned cookie store shared by
`document.cookie` and native HTTP requests. Cookies are held in memory and removed
when the renderer closes. This also packages the previously tested child-frame
and memory-only POST work. The existing browser toolbar remains the entry point.

The store enforces host/domain/path scope, Secure, HttpOnly, SameSite, expiration,
protected cookie-name prefixes and public-suffix boundaries. Request credential
omission drops outgoing cookies and ignores response cookies. Redirects recompute
their cookie header for the new URL; they cannot retain another origin's header.
Separate Set-Cookie fields are processed only after the full header block passes
validation, and the transport has already rejected unverified HTTPS. Quoted
values keep their wire representation. Expiration is bounded to 400 days even
though all storage is session-only.

The official PSL, including private hosting, wildcard and exception rules, is
pinned by input/output SHA-256 and compiled into the renderer. Domain spelling
at the legacy document.domain boundary is case-normalized; non-ASCII spellings
at that boundary fail closed. Normal URL hosts use canonical Punycode. Protected
cookie prefixes are matched without regard to case, while cookie identities
retain their original case. Sources: [HTTP cookie standard](https://httpwg.org/specs/rfc6265.html),
[current HTTPWG cookie work](https://httpwg.org/http-extensions/draft-ietf-httpbis-rfc6265bis.html),
and [official PSL](https://publicsuffix.org/list/).

Limits: 128 cookies per renderer, 4096-byte Set-Cookie fields, 2048-byte request
header blocks and 4096-byte memory-only POST bodies. An oversized outgoing cookie
header fails explicitly rather than being truncated. Cookie names/values accept
the ASCII cookie syntax; Domain attributes require ASCII/Punycode. CHIPS
Partitioned cookies are rejected until partition keys are implemented. The newer
Cookie Store API is hidden instead of advertising its unimplemented methods.
There is no persistent browser profile, file upload or automatic CAPTCHA solver.

Validation uses host scope/expiry/prefix tests and 10,000 deterministic malformed
and valid fields, plus the real ARM64 WebCore cookie parser integration, Page,
DOM, JavaScript, iframe clicks and SACX POST path under QEMU. It verifies shared
DOM/network storage, hidden HttpOnly state, credential omission, unverified TLS
and malformed-header rejection, Set-Cookie response-header filtering, domain
denial, sandbox/CORS-related origin isolation, images, navigation and cleanup.
The QEMU network is modeled; these checks do not demonstrate Google acceptance.

Google's [reCAPTCHA documentation](https://developers.google.com/recaptcha/docs/faq)
describes the necessary verification cookie. A real challenge must still be
completed by the user in the guest. No challenge response is synthesized here.
Further missing APIs may be exposed by that test. F12 now reports child-frame
load counts in addition to paint CPU/wall timing, without logging cookie values.

Use fresh build outputs for later milestones; retain sealed evidence. With the
existing pinned engine archives/toolchain, preparation in PowerShell is:

```powershell
New-Item -ItemType Directory -Force build/dihscover-cookie-validation
Invoke-WebRequest https://publicsuffix.org/list/public_suffix_list.dat -OutFile build/dihscover-cookie-validation/public_suffix_list.dat
python tools/prepare_browser_psl.py --input build/dihscover-cookie-validation/public_suffix_list.dat --output build/dihscover-cookie-validation/browser_psl.inc
```

Keep the pinned data after downloading; the official service requests no more
than one download per day. The packaged source archive contains the exact input,
generated table and MPL 2.0 notice for offline reconstruction.

Build/test from WSL Ubuntu:

```sh
python3 tools/webcore_dihos/build_cookies.py
python3 tools/webcore_dihos/test_cookies.py --include-page
```

Build the regular frontend after the renderer has finished:

```powershell
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -OutputDirectory build/dihscover-cookie-validation/frontend -EngineDirectory build/dihscover-cookie-validation/renderer
```

This package includes the matching operation-40 kernel built from the isolated
`build/capt/src` snapshot, preserving unrelated native graphics/Wi-Fi work and
the working checkout kernel. Its school CA is unchanged. With the VM off, update
from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-session-cookies-20261103
```

Open Dihscover normally and revisit the verification page, or Google's public
demo at `https://www.google.com/recaptcha/api2/demo`. Complete any real challenge
yourself. Test normal browsing, Stop/Reload and closing/reopening too. Cookies
reset on close, so a later session may require verification again.
