# Dihscover: web applications and muted video

Decision note, 2026-10-27. Browser baseline: `0182512ddca68b6b63255c47c141db1d2dc9fbae` on main. The user reports that the previously crashing links now work. This is useful guest confirmation, not a general stability or latency certification.

## Target and current recommendation

Keep the improved NetSurf browser usable while prototyping a separate modern engine. The target is native rendering inside DihOS on ARM64 Hyper-V and native hardware: interactive feeds and web apps first, then **muted YouTube playback and seeking**. Audio devices, output, mixing and audio-driver work are explicitly out of scope. Do not bolt an unrelated JS interpreter onto NetSurf: DOM, event loops, CSS and browser APIs have to work together.

**WebKit is the leading prototype candidate, not an accepted DihOS port.** The prior host comparison favors its rendering over Servo on the tested pages. WPE supplies an embedded integration API and a mature media path, but [WPE currently targets Linux](https://wpewebkit.org/about/faq.html). A DihOS WPEPlatform display adapter alone does not supply libc, threads, processes, networking or graphics dependencies. Assess the Linux dependencies before choosing a WPE-derived platform port versus a smaller custom WebKit platform layer. Keep an upstream-facing patch set.

Re-evaluate Ladybird alongside it on the same ARM64 host corpus. Its [August media report](https://ladybird.org/newsletter/2026-08-31/) describes YouTube improvements, but those are upstream platform results, not DihOS results. Its [porting guide](https://raw.githubusercontent.com/LadybirdBrowser/ladybird/master/Documentation/Porting.md) requires platform/process/socket/IPC integration. Servo remains a candidate; the earlier rendering gaps and Rust/SpiderMonkey/platform dependencies still need assessment. Do not rank either by implementation language or project origin.

Pinned WPE source candidate: **2.54.2**, [released 2026-10-23](https://wpewebkit.org/release/wpewebkit-2.54.2.html), archive SHA256 `f3284e19e47fcb5ac8c63c2e1c113dbd65b2423eb8a545f5b345707829004f56`. Its JSCOnly build passed the [ARM64 Linux runtime probe](wpe-arm64.md); this does not constitute a DihOS port. The existing Windows WebKit/Servo pins are in [the initial evaluation](README.md).

## Evidence added now

`tools/browser_fixtures/webapps.html` is an owned, deterministic interactive feed. Its host WebKit test passes ten checks: asynchronous JSON fetch/DOM creation, input filtering, append navigation, promises/timers, mutation observation, local storage, IndexedDB, Canvas, CSS grid and animation frames. External requests are blocked by the host test. [Recorded result](webkit-webapps-2336.json).

The tested Playwright WebKit 26.5 Windows build reports `MediaSource` unavailable. Its codec capability strings are not evidence of decoded playback. **No video playback or YouTube success is claimed.** This build is useful for web-app fixtures, but cannot stand in for WPE/GStreamer media evaluation. Host screenshot/timing results do not predict guest performance.

Run the repeatable host check with the installed pinned runtime:

```powershell
$env:PLAYWRIGHT_BROWSERS_PATH = "$PWD\build\browser-deps\playwright"
node tools/browser_webapps_probe.cjs <absolute-path-to-playwright-package>
```

Or serve both layout and web-app fixtures for guest/manual testing:

```powershell
python tools/browser_engine_probe.py --serve --bind 0.0.0.0
# Open http://<host-address-reachable-from-the-guest>:8765/webapps.html
```

The current NetSurf build deliberately does not run this fixture's JavaScript. Keep that limitation visible. WSL/Windows engines are evaluation tools only; the shipping browser must not depend on a host rendering service. On Windows, cloning the vendored upstream test trees may require `git -c core.longpaths=true clone ...`.

## Work packages, in dependency order

| Package | Implementation boundary | Completion gate |
|---|---|---|
| 1. Browser process foundation | Build on existing process bookkeeping and ARM64 EL0 entry work. Add schedulable EL0 threads, timer preemption with full register state, per-process mappings/protection, guarded stacks, TLS and blocking wait/wake primitives. Route privileged calls through a bounded syscall/service boundary. | An API-free infinite user loop can be stopped; a fault kills only its process; pointer/terminal remain responsive; repeated create/fault/close restores memory and handles. The present SACX fibers/compiler checkpoints are not this isolation. |
| 2. Engine runtime and dependency proof | Target libc/C++ runtime, file/memory/timer/thread APIs, IPC and shared surfaces. Pin and build the chosen source/dependencies for ARM64. Start JSC without JIT; add protected executable mappings and a JIT policy only later if needed for speed. | Minimal engine runtime and one local HTML/CSS/JS page run inside DihOS. Document unsupported symbols/dependencies instead of stubbing success. No untrusted engine code executes in kernel space. |
| 3. Native view and interactive web apps | Retain Dihscover chrome; forward keyboard, pointer, wheel, focus and text input to an engine adapter. Exchange owned software surfaces/damage through IPC. Integrate fonts, images and persistent origin storage. | Local web-app fixture passes in DihOS; scrolling/resizing/caret/selection work. Close/stop/navigation replacement are bounded. Capture input-to-present p95 below 50 ms and no browser stall over 100 ms in controlled tests. These are targets, not measurements. |
| 4. Concurrent resource loading | Replace the single global TCP/TLS exchange with bounded per-connection state and async readiness/cancellation. Extend request headers, cookies, redirects, compression, caching, CORS/fetch semantics, range requests and verified TLS roots/time/entropy as required. Preserve USB/NetVSC transport ownership. | Controlled fetch/storage/form fixtures pass; parallel subresources, slow peers, loss and cancellation leave desktop responsive. Persistent sessions are tested only after certificate validation and cookie isolation work. |
| 5. Muted media | Port the selected media backend. For WPE, assess GStreamer dependencies and software video decoders. Start with an owned video-only clip, then an A/V container with audio output disabled/discarded. Audio tracks must not make the pipeline wait for an absent device. Use a monotonic media clock. | A local clip visibly advances, pauses and seeks; then an HTTP clip and an MSE segmented clip do so. Verify real decoded frame progress, not just `canPlayType`/`isTypeSupported`. Begin at a proposed 480p/30 target; measure before raising quality. |
| 6. YouTube and feeds in guest | Test the ordinary site through the port, including JS player controls, resource fetching, MSE, codec negotiation, buffering and gesture/autoplay behavior. Retain native software rendering on Hyper-V. | A logged-out search opens a video; muted frames advance for ten minutes; pause/resume/seek work; repeated navigation/close does not leak or freeze the OS. Test feeds independently; do not treat one playable video as broad web-app support. |

Packages 1 and 2 are the first implementation milestone. ARM64 host compatibility/media experiments can run alongside OS work, but cannot replace the guest gates. Do not estimate a small patch or promise a date before the dependency/runtime prototype exists.

## Media details and performance budget

WebKit's [MSE interface](https://docs.webkit.org/Deep%20Dive/Modules/MediaSourceExtensions.html) separates JS-managed segment buffering from platform parsers/decoders. Its [WPE multimedia backend uses GStreamer](https://docs.webkit.org/Ports/WebKitGTK%20and%20WPE%20WebKit/Multimedia.html). Supporting a JS engine or a video tag alone is insufficient.

Investigate WebM/VP9 first and MP4/AVC where required by the measured corpus; add AV1 only when compatibility and CPU measurements justify it. Muted playback still needs valid container/timestamps and handling of multiplexed audio tracks, but no DihOS audio output is required for this milestone. No DRM implementation is included. Do not claim every YouTube stream uses one codec or assume a desktop codec report matches the target build.

Record JS/layout/paint/present CPU time, decode time, dropped frames, peak resident memory, network buffering and request concurrency separately. Start with one tab and a documented memory limit; measure the 2 GiB VM before deciding its budget. Shared surfaces and bounded buffer reuse should avoid copying an entire desktop for each video update. Hardware video acceleration and a higher-resolution target follow measured software results, not an assumption that the native GPU driver already supplies video decoding.

## First concrete implementation task

Implementation in progress: [ARM64 user-runtime fixtures](../dihscover-user-runtime.md) passed in QEMU and Hyper-V, including 6,607 preemptions of an API-free loop in the reported guest run. The [restricted static ELF path](../dihscover-user-elf.md) also passed the guest exit/loop checks after address-switch synchronization was corrected. A [first SACX userspace bridge](../dihscover-sacx-userspace.md) provides checked buffers, clock, log, yield/sleep and exit in QEMU and the reported Hyper-V run (twelve calls, expected success message, task exited). Preserve SACX as the intended privileged API, with kernel-enforced app grants and scoped file access in later services. Full permissions/policy configuration is deferred at the user's request. Runtime libraries, general loading/dynamic linking, IPC and the engine port remain outstanding. NetSurf has not moved into EL0.

The next runtime contracts are driven by the [pinned WPE/JavaScriptCore ARM64 source/build probe](wpe-arm64.md). The [owned SACX memory service](../dihscover-user-memory.md), [C++ startup](../dihscover-user-cpp.md), [TLSF arena](../dihscover-user-allocator.md) and [selected libc++ slice](../dihscover-user-library.md) pass their reported Hyper-V fixtures. The new [native threads/TLS and timed wait layer](../dihscover-user-threads.md) passes ARM64 QEMU, including API-free worker preemption and fault/cancel cleanup; its reported Hyper-V gate passed (25 joined workers, plus 24 API-free preemptions). The next [C++ lifetime layer](../dihscover-user-lifetime.md) implements local-static guards, thread-local destructors and synchronized global finalization; its reported Hyper-V gate passed (exit 0x51, all three workers joined and heap reclaimed). The [selected standard synchronization adapter](../dihscover-user-sync.md) adds mutex/recursive mutex/call_once over native SACX waits; its reported Hyper-V gate passed after the lock fast-path correction (exit 0x53, all three workers joined). The [standard-thread/key adapter](../dihscover-user-stdthreads.md) now passes ARM64 QEMU, with its reported Hyper-V gate passed (exit 0x54 and 16 joined workers). The [larger image/heap layer](../dihscover-user-large-images.md) now admits 32 MiB files, 64 MiB image spans and 16 MiB guarded regions with incremental admission (reported Hyper-V gate passed: exit 0x56, 2,053 fixups and reclaimed heap). Further C/ICU support, standard-library/WTF thread adaptation and event/IPC services remain before a native WebCore view. Retain the improved NetSurf build as the comparison baseline. Complete an ARM64 host WPE media build and the missing Ladybird corpus run before accepting a final engine choice.

The [freestanding DihOS JSC probe](jsc-dihos.md) compiles the embedding and WTF
allocator objects and links the SACX standard-thread fixture without unresolved
imports. The [target ICU build](icu-dihos.md) now compiles all 456 common/i18n
sources and packages genuine Unicode data. Its required [RTTI runtime](../dihscover-user-rtti.md)
passes native EL0 QEMU. With the Unicode headers present, source probes expose
the remaining floating-point environment, clock, native thread and register
context contracts. The engine has not linked or run inside DihOS; this remains
separate from the successful Linux JSC runtime evaluation.

The [native VM adapters](../dihscover-user-vm.md) now execute the actual pinned
bmalloc and WTF memory interfaces through SACX in EL0 QEMU. The build has passed
the allocator's POSIX memory include and reaches the separate entropy-source
fallback. The latter needs a genuine native kernel random service, alongside
the outstanding thread/register, floating-point and clock work.

That [native random service](../dihscover-user-random.md) is now implemented:
firmware/RNDR seed capture, BearSSL HMAC-DRBG and an owned-buffer SACX call feed
the actual bmalloc/WTF adapters. Host and ARM64 EL0 tests pass, including the
emulated RNDR instruction path and fail-closed behavior. Hyper-V/native physical
source availability still needs a fresh-boot check. The build reaches the
thread/register-context gap; JavaScript still does not execute inside DihOS.

The [native context/platform layer](../dihscover-user-context.md) now supplies
real stopped-thread registers, stack bounds, detach, FP state, UTC clocks and
calendar conversion. Thirty ARM64 integration checks pass, including actual
WTF synchronization/clocks and rejected file handles. The full thread-object
adapter cross-compiles. The build checks also now retain freestanding C++ entry
points and explicitly reject missing dependencies. A native JSC executable,
WebCore/DOM integration and media support remain ahead of JavaScript sites and
YouTube playback. The shipping browser is unchanged by these experiments.

The [first native JavaScriptCore execution gate](native-javascriptcore-20261030.json)
now passes in ARM64 EL0 QEMU: context creation, JavaScript evaluation to 4950,
context release and complete process-memory cleanup, alongside all 33 preceding
runtime groups. The 8 MiB structure-heap profile fits the current charged
reservation model. Next validate that exact engine on Hyper-V, extend isolated
JavaScript/GC/cancellation fixtures, and supply sparse VM reservations and the
event/IPC services needed by WebCore. A working standalone JS engine does not
add DOM or JavaScript website support to NetSurf.

The standalone arithmetic test has since passed in the
[user-reported Hyper-V run](native-javascriptcore-hyperv-20261030.json).
The [expanded native suite](native-javascriptcore-suite-20261030.json) now passes
in QEMU with modern language features, ICU Intl, promise microtasks, full GC,
protected roots and isolated contexts. A real infinite-JavaScript executable
also passes timer preemption, cancellation and allocation cleanup. The
[guest test commands](../dihscover-user-javascriptcore.md) cover both profiles.
Sparse virtual reservations and WebCore's event/IPC/platform services remain
the next implementation work before a native web view.

The [sparse kernel memory service](../dihscover-user-sparse-vm.md) now passes
three- and four-level ARM64 VM tests, including the existing JSC binary
compatibility gate. It separates GiB-scale virtual reservations from physical
commitment, reclaims backing on decommit, and checks rollback, isolation and
hardware permissions. Its
[Hyper-V success fixture](sparse-vm-hyperv-20261031.json) now reports exit 0x65
and zero remaining heap reservations, with 72 KiB peak committed payload. The
SDK browser VM adapter has now been
[migrated and validated with the rebuilt native engine](../dihscover-user-sparse-adapter.md).
The engine uses its upstream 4 GiB structure-heap reservation with about
3.2 MiB peak committed heap pages in the expanded suite. All 39 integration
groups pass, including desktop-paced GC and busy-script cancellation. The
[reported Hyper-V suite](native-javascriptcore-sparse-adapter-hyperv-20261031.json)
has now completed with exit 0x57, ten preemptions and zero remaining heap.
Next implement WebCore's event/IPC/resource services and an isolated native
web view. Standalone JSC remains separate from
the shipping NetSurf browser; JavaScript sites and video playback are not yet
enabled.

The [native event-loop foundation](../dihscover-webcore-event-loop.md) now
passes 40 ARM64 integration groups: actual generic WTF RunLoop/timers, bounded
cross-thread dispatch and cancellation, owner-thread capture cleanup and a
native timer calling protected JavaScript with a promise checkpoint. An
isolated pinned tree-removal fix resolves a checked-pointer timer shutdown
failure. The verified Hyper-V package retains the working kernel and NetSurf
browser. Native WebCore configuration, process IPC, resource loading, DOM and
painting remain the next gates; this does not enable JavaScript webpages.

The [reported Hyper-V event-loop fixture](native-webcore-eventloop-hyperv-20261031.json)
also passes: exit 0x67, two joined worker threads, three timer preemptions and
zero remaining reserved/committed heap. Continue with checked asynchronous
resource loading and the native WebCore page/rendering configuration.

The [checked EL0 HTTP service](../dihscover-user-http.md) now implements
capability-gated asynchronous GET, owned URL/response buffers, process- and
generation-bound handles, checked metadata/body copies and stop/fault/exit
cleanup through the existing single transport owner. Forty-one ARM64 groups
pass with modeled HTTP transport, alongside host continuation/broker tests and
an isolated kernel build. `proc:run-net` explicitly grants network access;
ordinary launches remain denied. The
[reported live Hyper-V GET](native-user-http-hyperv-20261031.json) now passes
with exit 0x68, 41 SACX calls and the explicit network grant.
The 64 KiB cumulative response cap and missing TLS peer authentication are
explicit bring-up limits, not a complete WebCore Fetch backend. Owner-thread
resource completion, native WebCore configuration, DOM and painting remain.

The [native owner-thread resource client](../dihscover-webcore-resource-client.md)
now connects the HTTP syscalls to actual WTF timers and the bounded dispatch
queue. Forty-two ARM64 groups pass, including callback/promise ordering,
reentrant cancellation, weak lifetimes, deadlines, protocol failures and queue
backpressure. Unverified TLS is rejected by default; the public diagnostic
fixture explicitly allows the existing transport limitation. The package
reuses the live-tested kernel. The
[reported Hyper-V callback fixture](native-resource-client-hyperv-20261031.json)
now passes with exit 0x6a, two timer preemptions and zero remaining heap.
Next bring up the native WebCore build and Page/graphics/input/surface integration.
DOM execution and JavaScript website support remain outstanding.

The [native WebCore configuration](../dihscover-native-webcore-build.md) now
passes for `PORT=DihOS` and `aarch64-none-elf`, with actual target XML, SQLite
and zlib archives. Eight WebCore core units compile, including HTML/CSS
tokenizers and the native resource-error type with its upstream implementation.
This is compile evidence only. The complete WebCore library, native request/
response/certificate contracts, Page/graphics integration and local DOM/script
execution remain outstanding. The shipping browser and guest package are unchanged.

The [native resource-types milestone](../dihscover-webcore-resource-types.md) now
links and executes actual WebCore request/response/error metadata, strict HTTP
header parsing and body delivery over checked SACX calls. Forty-three ARM64
integration groups pass with modeled transport, including manual redirects,
MIME/charset, TLS rejection, capability denial and reentrant cancellation.
The live Hyper-V resource-types fixture also passed (exit 0x6f and 182 SACX
calls). Frame/ResourceHandle hookup, local DOM/script pages and software graphics
remain; the shipping NetSurf browser still does not execute modern JS webpages.

The [native Cairo/Pixman graphics bring-up](../dihscover-native-cairo.md) adds
an image-only software backend, real SACX mutexes, native jump buffers and
preemptible ARM64 NEON drawing. The standalone EL0 fixtures check pixels,
threading and both recoverable and process-terminating allocation failures.
WebCore's Document, HTML document parser, RenderView and Cairo GraphicsContext
compile, with generic wrapper ownership and real SACX process identity. FreeType/
HarfBuzz fonts, complete Page/Frame integration and presenting interactive
HTML remain outstanding. No modern-site or YouTube support is claimed.

The [native font port](../dihscover-native-fonts.md) builds pinned FreeType,
thread-safe HarfBuzz and the native ICU adapter against the EL0 runtime. The
memory-font fixture passes its 31-group runtime/graphics/font suite, covering
the packaged Noto families, fractional metrics, Unicode clusters, ligatures,
combining marks, zoom, malformed fonts and concurrent rasterization. A separate
Cairo FreeType build connects shaped glyph IDs and positions to image painting.
WebCore font selection/fallback and its Fontconfig contract, complete
Page/Frame clients and document presentation remain outstanding. This does
not enable JavaScript websites in the installed NetSurf browser.

The [native font-selection milestone](../dihscover-native-font-selection.md)
now runs real Fontconfig memory-face registration, alias rules, coverage,
fallback sorting and concurrent matching. Its matched patterns paint through
Cairo with zero filesystem attempts across initialization and rendering.
The native XML build uses the correct thread-local errno/stdio ABI. All 46
ARM64 regression groups pass; ten actual WebCore font/Page/Frame/Document
units compile. Complete engine linkage, the process-owned page/font-registry
clients, local DOM/JavaScript rendering and window presentation remain.

The [local Page bring-up](../dihscover-native-page.md) now has an ARM64-compiling
client using upstream headless Page/Frame interfaces, a permanently offline
loader policy, DOM/script/layout assertions and guarded Cairo pixel checks.
Its separate unified engine build is still being brought up; a native-endian
Web Push parser dependency was corrected without adding socket APIs. Complete
archive linkage and executing the Page remain unverified. No installed-browser
or live-site capability is implied by this client compilation.

The [native crypto port](../dihscover-native-crypto.md) now builds OpenSSL
3.5.9 against SACX and passes 30 ARM64 runtime groups. This supplies the real
crypto dependency exposed by the full WebCore build: no placeholder EC/RSA
containers or successful crypto stubs. The new WebCore profile imports the
validated archive and upstream OpenSSL/PAL sources. Full Page linkage and
execution remain separate gates.

The [first native Page render](../dihscover-native-page.md) now passes its
29-group ARM64 runtime suite. The complete WebCore/JSC engine parses local
HTML, runs DOM/style JavaScript, lays out and paints with native memory fonts,
and exits with all process pages reclaimed. The actual guest pixels are
captured as a 640 x 480 PNG. This is an offline, offscreen Page diagnostic;
the installed Dihscover browser is unchanged. Frame/resource loading, native
window presentation/input, real browser platform services, bitmap codecs and
live-site compatibility testing are the next integration work.

The [owned-window milestone](../dihscover-native-window.md) now appends
capability-checked window syscalls and connects the local WebCore Page to
kernel-owned double-buffered surfaces. Its 29-group ARM64 run verifies queued
click -> DOM JavaScript -> CSS repaint -> second presented frame -> close,
with all process/window pages reclaimed. The KWINDOW/KGFX adapter builds in
an isolated desktop kernel. Hyper-V validation, keyboard editing, dynamic
viewport resizing, damage-based uploads and live navigation remain.

The first Hyper-V window attempt was still reading the executable at 7.6 MiB.
The [loading update](../dihscover-native-window.md#loading-responsiveness-update)
uses timed 4 KiB disk-read slices, bounded admission batching and readable
progress/read-latency diagnostics. Removing non-loadable symbols reduces the
deployment ELF from 121 to 98 MiB with identical loadable segments. The revised
29-group ARM64 window suite and loader host tests pass; Hyper-V latency remains
to be measured with the new package.

The user subsequently confirmed the revised Hyper-V window opened and the blue
box turned green when clicked. This confirms that local input/DOM/painting path;
it does not establish live-site or timing acceptance. The next transport work
adds [checked WebCore request headers and response deferral](../dihscover-native-http-requests.md).
Live Frame navigation and authenticated HTTPS remain pending.

The [request-boundary regression](native-http-requests-20261101.json) passes
33 ARM64 guest groups, focused host checks and the parser sanitizer corpus.
It exercises the new request descriptor through actual EL0/SVC/VM-copy admission,
WebCore header forwarding, response deferral/deadlines, cancellation and the
existing native Page click/repaint path. Transport is modeled in QEMU; live
Frame navigation and trusted HTTPS are not claimed by this proof.

The [native navigation preview](../dihscover-native-navigation.md) now connects
WebCore Frame/DocumentLoader/SubresourceLoader to the native ResourceHandle and
SACX transport. Its [30-group ARM64 regression](native-navigation-20261101.json)
checks a redirect, external CSS, external JavaScript changing painted layout,
clicked link navigation, Stop on a stalled load, Reload and closing with HTTP
pending. It reclaims the owned process, window and network resources. The
isolated desktop kernel and 162-file deployment package are verified. HTTP
transport and the compositor sink are modeled in QEMU; live Hyper-V validation,
authenticated HTTPS, address/keyboard input, bitmap codecs and broader browsing
remain. The shipping NetSurf browser is retained alongside `UserRuntime/web.elf`.

The [verified HTTPS update](../dihscover-native-https.md) embeds 118 pinned
Mozilla/curl roots, removes the unknown-issuer exception, and uses the existing
seeded random service. Kernel streaming and checked header/body copies enforce
the actual authenticated transport result. Real BearSSL tests cover valid RSA
and ECDSA chains and invalid certificates; live host probes authenticate four
public sites and reject three bad-certificate endpoints. The 30-group ARM64
browser regression passes with modeled HTTPS outcomes. The 166-file package is
ready for a live Hyper-V HTTPS test; address entry, larger responses, bitmap
codecs and media remain.
