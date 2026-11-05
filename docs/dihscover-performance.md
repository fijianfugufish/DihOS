# Making Dihscover smooth

## What production browsers do

Chromium's rendering pipeline separates DOM/style/layout from compositing and
raster work. It retains display lists and raster tiles, so eligible scrolling
and visual effects can run on a compositor thread without repeating layout or
painting the page. Video frames join composition through a separate path.
[RenderingNG architecture](https://developer.chrome.com/docs/chromium/renderingng-architecture)
and [compositor design](https://www.chromium.org/developers/design-documents/compositor-thread-architecture/).

Chromium's media pipeline separates data loading, demuxing, decoding and timed
presentation. It has software decoder backends and platform hardware decoders
such as D3D11, VA-API and MediaCodec. Frame selection is driven by a playback
clock; decoding and displaying frames are distinct operations.
[Chromium media architecture](https://www.chromium.org/developers/design-documents/video/).

These are architectural references, not evidence that Windows' decoder APIs are
available inside DihOS. The current Hyper-V synthvid driver presents CPU-built
framebuffers. GPU composition and hardware video decoding need their own guest
drivers, capability checks and interfaces.

## Findings in this checkpoint

- `MediaPlayerDihOS.cpp` serializes decoding on the renderer main thread.
  `sample_decoder.c` explicitly sets one decoder thread and disables decoder
  threading. `build_media_decoder.py` disables assembly, inline assembly and
  runtime CPU detection in the native decoder build.
- Decoded YUV frames are converted to BGRA in software. `Player::present()`
  allocates a Cairo surface and copies frame pixels again. Page painting and
  the checked kernel upload add more work.
- Large scrolling updates repaint the viewport. Small damage uploads reduce
  transfers, but are not a retained tile compositor or independent scrolling.
- JavaScriptCore currently uses its non-JIT execution path. Preemption keeps
  other OS work alive; it does not make expensive browser callbacks finish fast.
- No decoded-versus-presented FPS, per-stage timings or selected live stream
  dimensions/bitrate were captured in the user's report. Poor quality may be
  adaptive stream selection or upscaling; it is not yet a confirmed decoder bug.

## Implementation order

1. **Measure one repeatable workload.** Record stream codec/dimensions, buffered
   duration, decoded/presented/dropped FPS, decode and color-conversion time,
   paint and presentation time, upload bytes, JS/layout time and input latency.
   Compare a local 480p/720p H.264 clip, a repeatable scrolling page and a live
   YouTube watch page. Keep cold and warm loading measurements separate.
2. **Improve software decode first.** Enable verified ARM64/NEON decoder and
   conversion paths where the CPU supports them; compare pixels and timestamps
   with reference clips. Preserve FP/SIMD context across preemption. Check actual
   throughput before advertising more codecs or forcing higher stream quality.
3. **Separate media work.** Give the decoder a bounded worker-owned queue and
   generation-based cancellation. Keep WebCore DOM/layout on its owner thread.
   The current user-runtime service runs tasks on the boot CPU; extra user
   threads improve scheduling isolation, but multicore throughput also requires
   scheduler work. Thread creation alone is not that throughput improvement.
4. **Present on time and reduce copies.** Select frames against a monotonic
   clock, bound queued decoded frames, and avoid converting/presenting obsolete
   output. Preserve H.264 reference decoding; arbitrarily dropping encoded
   packets corrupts dependent frames. Reuse/refcount surfaces so the producer
   cannot overwrite pixels still being painted. Start with redundant copies
   within userspace; any shared presentation buffers must remain kernel-owned,
   permission-checked handles with explicit ownership transitions.
5. **Cache page rendering.** Add bounded raster tiles and immutable compositor
   state for scrolling, fixed/sticky elements, clipping and video/controls.
   A video update should not require painting unrelated page content. Do not
   enable the unsupported graphics-layer factory or move live DOM objects onto
   arbitrary threads. Add deterministic screenshot/hit-testing fixtures before
   introducing independent scrolling.
6. **Optimize script and loading bottlenecks identified by profiling.** A JSC
   baseline JIT needs checked executable-memory support, W^X transitions, cache
   maintenance and cancellation; it must preserve the SACX security model.
   Resource loading also needs measured connection reuse, concurrency, caching
   and bounded decompression work. Investigate hardware composition and decode
   after the software path is profiled and stable on Hyper-V and native hardware.

Initial targets are stable 480p30 software playback on a specified VM, responsive
scrolling during playback, bounded queues/memory, and no navigation/cancellation
leaks. These are future acceptance targets, not measured current performance.
Audio output remains outside this milestone.
