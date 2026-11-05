# Native local Page bring-up

This experimental target joins WebCore's actual Page, LocalFrame, HTML parser,
DOM, JavaScriptCore, CSS layout and Cairo software painting in an isolated
ARM64 process. The installed NetSurf browser is unchanged. Compilation is not
evidence that HTML rendering has executed; the build reports keep those states
separate.

The local fixture writes an owned HTML document. Inline JavaScript changes a
heading and a box's CSS width. It checks the DOM text, 192 by 64 CSS layout,
an exact background pixel, text ink against a known white surface, and guards
around the 640 by 480 image. It uses packaged memory fonts and rejects font
filesystem attempts. Its intended successful exit status is 115; execution
has not yet been validated.

WebCore's upstream headless clients supply frame/editor/chrome interfaces.
The fixture's local-only loader reports offline, completes rejected resource
requests, and returns explicit errors for synchronous loads, pings and
preconnects. Clipboard/media/blob/push strategy use fails the diagnostic.
These policies are specific to this local fixture. They do not implement
network navigation, origin policy, cookies or browser UI.

Fonts and the strategy registry retain process-lifetime backing because
WebCore caches may outlive an individual Page. The Page and image surface
have explicit scoped teardown. Repeated-navigation cleanup and process page
accounting still require execution tests.

## Build sequence

Run in ARM64 WSL from this checkout, after building the verified native
runtime, font selection and graphics dependencies:

```sh
python3 tools/webcore_dihos/configure_native.py --engine --openssl
python3 tools/webcore_dihos/compile_native.py --report build/webcore-engine-openssl/report.json --target WebCore --timeout 1800
python3 tools/webcore_dihos/compile_page_fixture.py --report build/webcore-engine-openssl/report.json --capture
python3 tools/webcore_dihos/build_page_fixture.py --report build/webcore-engine-openssl/report.json
```

The archive compilation is incremental; rerun it after a bounded timeout or
after correcting a port failure. Do not configure another profile against
the shared source tree while Ninja is building it.

The engine profile uses unified sources and JSC/WTF precompiled headers.
WebCore's optional prefix headers assume POSIX pthread/setjmp headers, so
WebCore uses its supported header-independent build. Both its already
declared main target and subsequent subtargets disable prefix headers.
The original diagnostic profile and recorded font-selection proof remain
separate.

The Page compiler uses a real command from this engine's CMake database,
replacing only the source/output paths. It checks the configuration and
platform hashes and rejects precompiled-header commands. The linker requires
a successful matching engine build, replaces the older diagnostic engine
archives together, flattens thin archives to hash their object bytes, embeds
the packaged fonts, and runs the production loader validator and import check.
It does not increase loader limits automatically.

Reports and failure logs live in `build/webcore-engine-native/` and
`build/webcore-headless-page/`. A passing compile/link does not set
`dom_executed` or `page_rendered`. Those require a native runtime test of the
final executable. A Hyper-V package and test command follow only after that
test passes.

For the OpenSSL engine profile, its configuration and archive logs are in
`build/webcore-engine-openssl/`. The local Page has no network or file grant.
Its DNS checks require unsupported direct resolution to complete with an
error exactly once; advisory DNS prefetch does no work. SACX HTTP retains
ownership of transport DNS.

The `--capture` build exports indexed RGB chunks from the guest's Cairo
surface over checked log calls. After linking, run the isolated ARM64 test
from PowerShell:

```powershell
.\tools\test_user_runtime_qemu.ps1 -NativePage -ReusePrograms -Python C:\Users\joshu\AppData\Local\Programs\Python\Python312\python.exe
& C:\Users\joshu\AppData\Local\Programs\Python\Python312\python.exe tools/webcore_dihos/extract_page_image.py
```

The extractor requires a passing runtime test and rejects missing, repeated
or reordered pixel chunks. It writes `build/webcore-headless-page/page.png`
and a capture provenance report. It only serializes guest pixels; HTML,
JavaScript, fonts, layout and painting execute inside DihOS.

This first-render profile has no bitmap image codecs or Cairo PNG encoder.
Unsupported bitmap decoding returns failure. The PNG capture is a diagnostic
host serialization step, not browser image support or a host page renderer.

The offline fixture's `headless_page_platform.cpp` is linked only into its
test executable. It supplies generic theme defaults and the actual fixed
surface geometry. Clipboard, cookies, public-suffix lookup, platform
accessibility and accelerated layers terminate the diagnostic if called;
they are outstanding port work, not successful service implementations.
Locale handling uses the upstream ICU backend. Automatic hyphenation is
unavailable through upstream's generic fallback.

The static link includes the matching `JavaScriptCoreJIT` object target.
Despite its name, this target contains interpreter and bytecode machinery
required with JIT disabled. It is sealed into a separate archive from this
same configuration; no old JSC binary is mixed into the Page.

The measured first Page executable is 126,866,112 bytes with a 103,006,208-byte
mapped image, 246,529 relative relocations and 1,264 TLS bytes. Loader ceilings
are now 128 MiB for file and image, and 262,144 relocations. Incremental
validation and relocation remain limited to 256 records per step. Native
process admission uses a 256 MiB budget that includes immutable source plus
inactive image during loading. W+X, imports, TLS limits, target checks and
transactional failure behavior are unchanged. The isolated QEMU test builds
these sources without replacing the installed kernel binary.

## Verified result: 1 November 2026

The complete engine and matching interpreter/bytecode objects now link into a
native ARM64 ELF with no unresolved imports. The [recorded 29-group runtime
suite](browser-evaluation/native-page-render-20261101.json) passes inside
DihOS's isolated QEMU kernel and EL0 SACX runtime.

The test parses owned HTML, executes inline JavaScript that changes an h1's
text and a div's CSS width, verifies the DOM and 192 x 64 CSS layout, paints
through WebCore/Cairo using the packaged memory fonts, and checks text ink,
exact background pixels, white background and surface guards. Font setup and
painting make no filesystem attempts. The actual 640 x 480 guest surface is
captured in `build/webcore-headless-page/page.png`; the host only serializes
those RGB bytes. Scoped Page teardown and process exit release all owned
pages, and the kernel continues running.

The test creates the frame view before initialization, as required by
DocumentWriter. Native font-cache lookups that ask whether a cache exists
now inspect existing thread data without recreating it after TLS teardown;
this fixes the shutdown abort exposed by the first successful paint.

OpenSSL 3.5.9 supplies the actual upstream WebCrypto backend; its separate
[crypto runtime validation](dihscover-native-crypto.md) remains the prerequisite
for configuration. This local Page test does not claim live TLS, WebCrypto
API coverage, window presentation, website compatibility or media playback.
The installed Hyper-V package and working kernel binary are unchanged.

## Remaining integration

The [native window diagnostic](dihscover-native-window.md) now passes the
29-group ARM64 suite with checked kernel-owned frames, queued pointer input,
real JavaScript click handling, CSS repaint and close cleanup. The desktop
adapter links into an isolated kernel; the prepared Hyper-V package still
requires guest validation. It is a fixed-size local page, not live browsing.

Connect Frame/ResourceHandle loading to the existing asynchronous SACX
resource bridge, replace the diagnostic-only platform contract with real
permission-scoped browser services, and present owned surfaces in a DihOS
window. Route input, scrolling, navigation and cancellation, then test
repeated navigation, memory pressure and interrupted script responsiveness.
Live JavaScript site testing and media/video support follow those steps.
