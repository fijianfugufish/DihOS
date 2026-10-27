# Dihscover responsiveness work

The published Hyper-V baseline is `c8812d2` on `origin/codex/hyperv-arm64`:
https://github.com/fijianfugufish/DihOS/commit/c8812d2

The browser changes are experimental until the ARM64 guest tests below pass.
They do not enable JavaScript or replace NetSurf's CSS engine. The modern-engine
evaluation is in [browser-evaluation/README.md](browser-evaluation/README.md).

## Build, test and package

Run from the repository root with LLVM and Python 3 available:

```powershell
python tools/generate_sacx_api_guards.py --check
python tools/generate_browser_resources.py --check
python tools/verify_browser_dependencies.py
.\tools\test_browser.ps1 -Fonts
.\tools\test_hyperv.ps1
.\tools\test_hyperv_package.ps1
.\tools\build_browser_validation.ps1 -Fonts
```

The last command exports the published baseline plus an explicit source overlay
into a fresh temporary directory. It excludes the working-tree kernel and the
unrelated GPU, ripple and Wi-Fi changes. It builds that source snapshot, runs host
tests, and packages its new kernel, boot files, browser, fonts and licences using
the recovered `build/hyperv-assets` bundle. It leaves the VHDX untouched. Build
logs, snapshot identity and the final package path are recorded under
`build/browser-validation/`. `SOURCE.JSON` identifies the browser source overlay
inside the guest. Supply `-Python <python.exe>` if Python is not on PATH.

For an ordinary working-checkout build:

```powershell
.\build.ps1 -ProjectRoot . -ArmOnly -KernelAa64Out build\browser\KERNEL.ELF
.\sdk\sacx\apps\dihscover\build.ps1 -ProjectRoot .
.\tools\hyperv.ps1 -Action Package -IncludeDihscover -KernelFile build\browser\KERNEL.ELF -PackagePath build\browser-package-new
```

After shutting down the guest, use Administrator PowerShell to install the
validated package, substituting the path recorded in `result.json`:

```powershell
.\tools\hyperv.ps1 -Action Update -VMName DihOS -PackagePath <package-path>
```

Install the matching kernel and browser together. The browser checks for the new
ABI capabilities. Old SACX applications retain their existing entry points.

## Execution and ownership

SACX launch now queues owned paths and returns before reading the executable.
A separate core-0 loader continuation performs file reads, CRC validation,
mapping and relocation in slices of approximately 2 ms between complete kernel
operations. Queued/active launches can be cancelled and have a 60-second total
deadline; their private stack and file buffer remain owned until cleanup ends.
Task status and sparse `sacx: startup` / `Dihscover startup:` messages distinguish
executable reading, validation/mapping, preferences, fonts and engine startup.
Individual synchronous storage calls are still not preemptible: a stalled disk
request can exceed a frame budget before the loader reaches its next checkpoint.
No page session or browser cache is restored from disk by this frontend.

Request-start now copies URL, method and form data into an owned slot and returns
without entering DHCP, DNS, TCP or TLS. One core-0 continuation owns transport
operations. Polling checkpoints preserve the stack and protocol state between
approximately 2 ms slices. Existing deadlines and cancellation checks still apply.
Automatic DHCP/ARP maintenance runs on this same resumable owner when no fetch
is active, rather than requiring a remote worker. USB ownership stays on its
existing path; USB bulk-IN polling is bounded to 1 ms.
Network responses are bounded to 8 MiB per request and four slots per application.
The active receive path answers ARP requests for the guest during DHCP/DNS/TCP,
using a separate reply buffer to preserve retransmissions. Previously only the
idle task answered ARP, so a host neighbour-cache refresh during a fetch could
prevent replies reaching the guest. Browser error pages now retain the kernel's
DNS/TCP/TLS/HTTP failure detail, including TLS engine error numbers even when
routine network logging is suppressed. The reported DNS timeout still requires
guest verification after installing this fix.

Streaming APIs expose validated headers and stable decoded body prefixes before
completion. The browser feeds NetSurf at most 4 KiB per fetch poll. Identity and
chunked framing, informational responses and close-delimited bodies are tested.
The new browser path preserves response headers and redirect status for NetSurf;
older request entry points retain automatic redirects. Cancellation pins an
active request until its continuation stops. Closing an application defers arena
release until that owner is finished.

Application callbacks execute on private stacks. Dihscover and its engine code
are compiled with `trace-pc,bb,no-prune` checkpoints. The elapsed-time checkpoint
can suspend an API-free optimized infinite loop. Generated wrappers defer
suspension inside kernel API calls and explicit preemption guards. Closing the
window is also checked from the compiler hook, so cleanup need not wait for a
parser/layout callback to return. A fixed round-robin starting cursor prevents
long callbacks from permanently winning the frame budget.

This is compiler-assisted scheduling for instrumented code. It does not provide
interrupt-driven preemption of arbitrary EL1 code or a process sandbox. Long
kernel calls, including image decoding and cryptography, still need guest timing
evidence and further bounds before the 100 ms stall acceptance criterion can be
claimed. A general ARM64 timer/IRQ/EL0 scheduler remains prerequisite OS work.

## Text and painting

FreeType 2.14.1 and HarfBuzz 11.4.5 are linked into the browser. Measurement,
wrapping, hit testing and painting use the same cached shaped runs and 26.6
advances. Bundled variable Noto Sans, Noto Serif and Noto Sans Mono supply
proportional/mono families and weight; italic is currently synthesized. Glyph
and run caches each have a 2 MiB memory bound. Missing base glyphs try the other
bundled faces. This font set is not comprehensive script coverage, and complete
mixed-direction Unicode processing remains unfinished.

The page stylesheet comes from the vendored NetSurf resources. Chrome styling
does not become a site stylesheet. The browser retains a private backbuffer,
unions invalidated regions, intersects nested clips with the paint damage, and
publishes completed regions. Rectangle borders, bounded line/circle drawing,
filled polygons, flattened paths and repeating bitmaps have host tests. Complex
vector antialiasing and several stroke details still need reference fixtures.
The compositor may still damage the whole image object when a region is published.
Scrolling is clamped to the current document extent; new content resets the
offset, so navigating from a long page to a short page cannot retain a position
below the new document.

The **Aa** toolbar control cycles the default sans family. Ctrl+plus/minus changes
zoom (50–300%). **X** while loading and Escape stop navigation. Preferences are
stored in `0:/Documents/Dihscover.ini`, outside the system update manifest:

```ini
sans=0
serif=1
mono=2
minimum=8
zoom=100
```

Family values are 0=Noto Sans, 1=Noto Serif, 2=Noto Sans Mono; minimum is CSS
pixels, 6–32. Changes to the file apply on the next launch. Remote `@font-face`
loading and a larger fallback collection are future work.

## Diagnostics and repeatable validation

SACX cursor requests are retained until the frame's cursor commit, after native
widgets choose their shapes. Native resize/button/text-input shapes take priority;
requests expire after 500 ms and cannot override another window or a closed app.
Dihscover refreshes its page hover request while the pointer is over its window.

The uninstrumented decimal conversion now bounds exponent work by magnitude
clamping and exponentiation by squaring. Integer power uses an unsigned magnitude
so the minimum signed exponent terminates. This removes two unbounded numeric
paths, but does not implement a complete standards-compliant math library;
fractional power remains an existing port limitation. These faults have not been
confirmed as the cause of the reported live-site link stalls.
Press **F9** with the page focused to toggle foreground/background images and
reload the document. This switch lasts for the browser session; defaults enable
images on the next launch. It provides a controlled comparison for persistent
link hangs because the current image decoding API remains a synchronous kernel
call. It does not establish an image-decoder diagnosis by itself.

Press **F12** in the document area to write inclusive active parse, style, layout,
paint, publish and fetch-dispatch timings, plus heap usage/peak, to the terminal
log. Scheduler pauses are subtracted from these application timings. Nested
phases overlap; do not add them as exclusive CPU times. `hv:diag` also reports
network-service slice count, active time, maximum slice, active/queued requests
and the existing desktop/render/input diagnostics. Routine per-callback disk
tracing is disabled; `DIHSCOVER_TRACE` enables it for explicit debugging.
The final F12 status line also reports page state, viewport/document dimensions,
scroll offsets, text/rectangle/bitmap draw counts since new content, and a count
of nonwhite samples in the painted buffer. This distinguishes missing paint from
surface publication problems. The example.com blank page was reproduced using
its captured HTML and the actual packaged ARM64 browser binary under WSL. Its
`body{margin:auto}` reached margin collapsing as `INT_MIN`; negating that unresolved
sentinel invokes signed overflow. Vertical automatic margins now resolve to zero
before collapsing, while horizontal automatic margins retain normal centering.
The captured page paints text after the fix. Guest confirmation is still needed.

Run `tools/test_browser_binary.ps1 -Regression` with an ARM64 Ubuntu WSL available
(or pass `-AppPath` explicitly). This runs the production SACX binary against an
owned deterministic fixture, comparing its output pixel-for-pixel with explicit
zero vertical margins, with images enabled and disabled. The previous package
fails this test; the corrected binary passes. The adapter uses mock UI/network
calls and real packaged fonts/engine/runtime, so it cannot validate Hyper-V
networking, kernel calls, or desktop responsiveness. `-Fixture` accepts a local
HTML capture, and PPM output/logs go to `build/browser-binary-repro`.

The ARM64 host adapter now uses the production kernel fiber implementation and
an actual 256 KiB stack with inaccessible guard pages. The captured apple article
uses about 46 KiB of that stack; this rules out stack exhaustion for that local
fixture, not all live navigation paths.

The guest subsequently recorded a fatal `SError` with implementation-defined
syndrome `0xBF53C002`. The root cause remains unknown. Main-document `net:get`
succeeds; that does not exercise browser subresources or their scheduling.
The mouse-release path delivered CLICK to both NetSurf tracking and click
handlers. HTML tracking delegates to the action handler, so a single hyperlink
release started two destination requests in the exact-binary reproducer. Clicks
now use only the click handler; tracking continues through the tracking handler.
`test_browser_binary.ps1 -Click -Fixture <local HTML>` exercises a real link
press/release before loading that fixture and asserts one destination request.
This fixes a demonstrated navigation fault, not a proven SError root cause.

Panic reports now include the kernel load base, interrupted stack pointer, live
app image ranges and relative PC/LR, app stack bounds, active/last completed API
and the active owned network URL. The original x16�x18 are not captured by the
emergency entry and are explicitly marked as scratch. Recording API names is
memory-only; no routine callback disk logging was introduced.
The panic renderer sends a best-effort full-frame synthetic display notification
without receive polling, mode negotiation or waiting, because the halted frame
loop cannot publish its damage. This improves crash visibility; it is not a fix
for the SError itself. If the video channel is unusable, the log is still needed.

The captured apple article, including twelve inlined stylesheets, completes on
both browser versions with images disabled. This does not reproduce or resolve
the user's persistent VM hang: live networking, external resource timing and
kernel behavior differ. Compare the exact HTTPS URL in `net:get` after updating
the package to separate transport work from browser processing. The lack of a
blue screen does not establish either a crash or a renderer-only fault.

The browser libc now uses milliseconds and firmware UTC rather than treating
the 60 Hz UI tick counter as milliseconds. UTC date conversion and HTTP date
formatting have leap-year and calendar round-trip tests.

Host tests cover production continuation switching, API-free loop suspension,
API guard deferral, owned request data, cancellation/release ordering, HTTP split
boundaries, raw header forwarding, redirects, clipping, font cache churn and heap
exhaustion/recovery. They are not guest latency measurements.

Serve `tools/browser_fixtures/layout.html` using
`python tools/browser_engine_probe.py --serve`. Its fixed-width paragraphs,
fonts, table, form, clipping, flex/grid and JavaScript probes expose unsupported
engine features. `/slow` emits delayed body fragments. For a guest test, bind the
server to a host address reachable through Default Switch and use that address
in Dihscover. The automated host probes are separate experiments.

Before accepting this milestone, run the matching package in Hyper-V and check:

- Move the pointer, type in another terminal and use window controls while loading
  a slow/stalled response, during large-document reflow, and during TLS setup.
- Stop, replace navigation and close during loading; repeat at least 100 times.
  Check retained heap/request/surface allocations and network recovery.
- Measure p95 input-to-present below 50 ms and no browser-induced stall above
  100 ms with controlled input timestamps and presentation timestamps.
- Capture fixtures at several viewport sizes/zoom levels and compare baselines,
  wrapping, forms, nested clips, image sizing and font families.
- Record cold/warm loads, first content, scrolling and peak memory against the
  previous build; repeat the native smoke test when the USB is available.

## Remaining plan work

The full acceptance plan is not complete. Guest execution is blocked in this
session by Hyper-V management permissions, including after tool escalation.
In addition to guest validation, outstanding work includes general ARM64
interrupt preemption, native USB TX timing, actual transport backpressure beyond
bounded response storage, gzip/deflate decoding, conditional request headers and
cookie transport, complete Unicode fallback/bidi, more vector reference cases,
and the Ladybird live-site evaluation. NetSurf JavaScript remains disabled;
modern site compatibility requires the next engine milestone.
