# Dihscover: web applications and muted video

Decision note, 2026-10-27. Browser baseline: `0182512ddca68b6b63255c47c141db1d2dc9fbae` on main. The user reports that the previously crashing links now work. This is useful guest confirmation, not a general stability or latency certification.

## Target and current recommendation

Keep the improved NetSurf browser usable while prototyping a separate modern engine. The target is native rendering inside DihOS on ARM64 Hyper-V and native hardware: interactive feeds and web apps first, then **muted YouTube playback and seeking**. Audio devices, output, mixing and audio-driver work are explicitly out of scope. Do not bolt an unrelated JS interpreter onto NetSurf: DOM, event loops, CSS and browser APIs have to work together.

**WebKit is the leading prototype candidate, not an accepted DihOS port.** The prior host comparison favors its rendering over Servo on the tested pages. WPE supplies an embedded integration API and a mature media path, but [WPE currently targets Linux](https://wpewebkit.org/about/faq.html). A DihOS WPEPlatform display adapter alone does not supply libc, threads, processes, networking or graphics dependencies. Assess the Linux dependencies before choosing a WPE-derived platform port versus a smaller custom WebKit platform layer. Keep an upstream-facing patch set.

Re-evaluate Ladybird alongside it on the same ARM64 host corpus. Its [August media report](https://ladybird.org/newsletter/2026-08-31/) describes YouTube improvements, but those are upstream platform results, not DihOS results. Its [porting guide](https://raw.githubusercontent.com/LadybirdBrowser/ladybird/master/Documentation/Porting.md) requires platform/process/socket/IPC integration. Servo remains a candidate; the earlier rendering gaps and Rust/SpiderMonkey/platform dependencies still need assessment. Do not rank either by implementation language or project origin.

Reference candidate for the next WPE source evaluation: **2.54.2**, [released 2026-10-23](https://wpewebkit.org/release/wpewebkit-2.54.2.html), archive SHA256 `f3284e19e47fcb5ac8c63c2e1c113dbd65b2423eb8a545f5b345707829004f56`. It has not been downloaded, built or run here. Record its actual dependency versions, flags and source patches before using its results. The existing Windows WebKit/Servo pins are in [the initial evaluation](README.md).

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

Specify the process/thread/syscall contracts against the existing `kernel/process`, `kernel/asm/aa64/aa64_user.c`, memory and IPC code. Produce one isolated ARM64 user task that can loop, fault, block, resume and close while the desktop runs. Validate it in Hyper-V and retain the improved NetSurf build as the comparison baseline. In parallel, complete an ARM64 host WPE media build and the missing Ladybird corpus run before accepting a final engine choice.
