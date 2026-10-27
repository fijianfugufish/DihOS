# Modern browser engine evaluation — 2026-10-27

**Recommended next port prototype: WebKit.** This is a provisional decision
supported by the host fixture/site runs below. No replacement engine runs inside
DihOS yet. Ladybird's corpus run, guest software-rendering costs and memory
measurements are still required before a final selection is accepted.

The experiments are deliberately separate from the shipping browser. Dihscover
still uses NetSurf 3.11 with JavaScript disabled; no standalone JS interpreter
has been attached to it.

## Pinned inputs and actual execution

| Candidate | Pin | Executed here |
|---|---|---|
| WebKit | Playwright 1.62.1, WebKit revision 2336 / version 26.5; upstream base `343e13bf22dca9d0ec227801419aab0f9001a32f` plus Playwright patches | Windows headless runtime, fixture + four public sites |
| Servo | Release `v0.7.0`; published Windows archive SHA-256 `49d4b2c2d3576f93fad70812ee9e42820f5b84da9103f2a633838109a2fcde53` | Windows servoshell with WebDriver, fixture + four public sites |
| Ladybird | Source `afb87db4b7f55029ec7b7d14d756184b67d22986` | Source/porting review and ARM64 Ubuntu prerequisite probe; no engine build/site run |

The tested WebKit is a patched Windows evaluation port, not an unmodified WPE
build or the separately inspected WebKit mainline revision. Its upstream base
is recorded by [Playwright's pinned upstream configuration](https://raw.githubusercontent.com/microsoft/playwright/v1.62.1/browser_patches/webkit/UPSTREAM_CONFIG.sh).
Servo's executable SHA-256 is recorded in `servo-0.7.0.json` in addition to the
published archive checksum. Both Windows test engines use x64 binaries on this
ARM64 host. Their timings cannot predict native DihOS or Hyper-V performance.

The Ubuntu prerequisite probe reported `aarch64` and approximately 15.4 GiB RAM.
It found no `cmake`, `ninja`, `clang++` or `pkg-config`, and no Qt6 package.
Ladybird requires a substantial toolchain setup before a meaningful run:
[upstream build instructions](https://github.com/LadybirdBrowser/ladybird/blob/afb87db4b7f55029ec7b7d14d756184b67d22986/Documentation/BuildInstructionsLadybird.md).
This is an unevaluated compatibility result, rather than a failed site test.

## Repeatable fixture and live-site observations

`tools/browser_fixtures/layout.html` exercises packaged fonts, fractional sizes,
baselines, a table and form, nested clipping, flex, grid and a JavaScript DOM
update. Both executed engines passed its JS/flex/grid/UTF-8 assertions. WebKit
also reported `document.fonts.status = loaded`; Servo did not expose that result.
The current NetSurf build is expected to expose JS and modern CSS limitations.

Raw measurements are retained in [Servo results](servo-0.7.0.json) and
[WebKit results](webkit-2336.json). Screenshots remain generated build artifacts
under `build/browser-engine-probe/`; they are not packaged into DihOS.

| Site | Servo 0.7.0 observation | WebKit 2336 observation |
|---|---|---|
| Wikipedia `/wiki/Web_browser` | Article DOM and links present | Article DOM and links present |
| GitHub `/servo/servo` | Repository content visible; initial return preceded final paint | Repository content visible, including refreshed file metadata |
| MDN `/en-US/docs/Web/CSS` | Content visible, but external-link labels overlap paragraphs and several icons appear as solid blocks | The inspected screenshot has aligned text and external-link/header icons |
| DuckDuckGo `?q=web+browser` | Search results present after settling | Search results present after settling |

Initial screenshots were incomplete even though navigation had returned. The
final corpus probes include a fixed ten-second settle interval for both engines.
The recorded navigation totals include that wait and differ in navigation-return
semantics. They are **not** load-speed rankings. Ad content, search results and
network responses also varied between runs. Public-page visibility does not
establish login, forms, application correctness or broad standards conformance.

Commands (download the pinned runtimes into `build/` first):

```powershell
python tools/browser_engine_probe.py --servo build/browser-deps/servo/servo/servoshell.exe --live
# Set PLAYWRIGHT_BROWSERS_PATH to build/browser-deps/playwright.
node tools/browser_webkit_probe.cjs <absolute-path-to-playwright-module> --live
```

For Ladybird, configure an ARM64 Linux toolchain matching the pinned build
instructions, build its headless/test frontend at the recorded source revision,
then run the same local fixture and site URLs. Store compiler/dependency versions,
screenshots, DOM assertions and actual process memory alongside these results.
Do not substitute LibJS-only tests for DOM/browser compatibility.

## DihOS capability and dependency report

These are port requirements inferred from upstream architecture and the local OS
sources, rather than demonstrated DihOS builds.

| Capability | WebKit | Servo | Ladybird | DihOS implication |
|---|---|---|---|---|
| ARM64/toolchain | Mature ARM64 implementations; new OS/port configuration and substantial C++ runtime | ARM64 Linux release exists; Rust target/std plus SpiderMonkey C++ support required | ARM64 Linux supported through its build system; modern C++ and Rust dependencies | Cross-compiling an ELF is insufficient; provide target runtimes and a repeatable dependency build |
| Threads/scheduling | Worker threads, locks, timers and event loops | Rust synchronization, layout/script/network threads | Event loops and worker threads across services | Add interrupt-driven preemption, thread-local storage, atomic wait/wake and reliable monotonic timers |
| Virtual memory | Reserved/committed regions, page protection, mappings, allocator support; JIT optional for initial interpreter-only bring-up | Rust allocator and SpiderMonkey VM/GC requirements | Per-process mappings and allocator/GC needs | Add guarded stacks, mappings, address-space ownership and permissions before JIT work |
| Processes/IPC | Web, network and GPU/service boundaries in normal multiprocess ports | Single-process configurations exist; IPC/multiprocess still part of the project | UI, WebContent, RequestServer and ImageDecoder; Unix-domain-socket-based IPC | Native handles/message passing or a deliberate POSIX compatibility layer are required |
| Networking | Port network/session/cache/cookie/TLS integration | Adapt network/resource loading and TLS dependencies | Adapt LibCore sockets/processes and RequestServer | Current serialized HTTP request API is a bring-up interface, not a full socket/network platform |
| Fonts/images | ICU, shaping, rasterization and image/font codecs depend on chosen port | Font discovery/shaping/codecs and platform integration | LibUnicode/LibGfx plus external libraries selected by the build | Package fonts/locales/licences and bounded decoders; do not use Windows font paths |
| Rendering | A software backend needs explicit port work; the WPE/GTK dependency stack is not already available | WebRender needs a supported graphics integration; software operation on DihOS is unproven | Skia/graphics integration and shared surfaces need porting | Render into guest-owned surfaces and measure CPU/memory bandwidth before selecting a backend |
| Memory | No guest measurements | No guest measurements | No executable measurements | Establish per-process/tab peak and OOM gates; do not infer memory use from archive size |
| Maintenance | Broad compatibility evidence, but a large dependency/platform surface and regular upstream security updates | Additional Rust/SpiderMonkey/platform work plus observed rendering gaps | Own OS abstraction is approachable, but compatibility and evolving dependencies still need measurement | Keep a small upstream-facing port, pinned inputs and recurring fixture tests |

Upstream references: [WebKit ports](https://docs.webkit.org/Ports/Introduction.html),
[Servo build system](https://book.servo.org/building/building.html),
[Ladybird porting interfaces](https://github.com/LadybirdBrowser/ladybird/blob/afb87db4b7f55029ec7b7d14d756184b67d22986/Documentation/Porting.md).

WebKit currently ranks first for the next experiment because of the inspected
compatibility evidence. Servo ranks second: its fixture and site coverage are
promising, but the observed MDN rendering faults and the Rust/SpiderMonkey/software
graphics port need investigation. Ladybird's priority remains provisional until
its build and corpus run exist; its unmeasured compatibility cannot be assigned
a numeric score. None is a small patch to the present SACX application.

## Concrete porting backlog and decision gates

1. Implement ARM64 timer/IRQ context switching with full register state, safe
   syscall boundaries and user address spaces. Validate arbitrary API-free user
   loops, blocked syscalls, closing processes and fault cleanup on Hyper-V/native.
2. Add process/thread primitives, TLS, mutex/condition/futex-style waits, monotonic
   timers, shared-memory mappings, file descriptors or native equivalents, and IPC.
   Gate this on deterministic stress tests and allocation/handle accounting.
3. Build a target C/C++ runtime and the selected engine's pinned dependencies.
   For a Servo alternative, include a DihOS Rust target/std and SpiderMonkey bring-up.
4. Prototype WebKit's minimal offscreen software view using local files and the
   fixture. Keep JavaScript in interpreter mode initially and keep engine-owned
   code out of the kernel address space. No host renderer in the production path.
5. Add asynchronous network/resource loading, cancellation, cookies, conditional
   caching, compression, verified TLS configuration and bounded backpressure.
6. Run the corpus and local fixtures inside DihOS. Record input/presentation
   latency, first content, cold/warm loads, scrolling, repeated-close leaks and
   per-process/tab memory. Compare with the improved NetSurf build.
7. Complete Ladybird's host evaluation and revisit the ranking. Accept a final
   recommendation only with comparable compatibility evidence and a measured
   guest software-rendering/memory budget. Replacement-engine implementation then
   becomes its own milestone.
