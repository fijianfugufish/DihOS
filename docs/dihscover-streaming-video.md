# Streaming media toward YouTube playback

The shipping MP4 player is preserved in
`build/dihscover-video-player-20261104`. YouTube playback is **not supported**
by that package or by this new experiment. Do not enable MediaSource capability
advertising until real WebCore append, track buffering and playback tests pass.

The first streaming component is `fragmented_mp4.c`: an incremental, single-owner
ISO BMFF segment assembler. It accepts split 8/16-byte box headers and payloads,
retains a completed initialization segment, and exposes each complete
`moof`/`mdat` segment combined with that initialization. A consumer must acknowledge
the immutable output before additional input can be consumed. The caller retains
the unconsumed input suffix. Abort clears partial media without discarding a
valid initialization; full reset releases it too. Parse failures remain sticky
until reset. Metadata-only service yields after 64 boxes.

The 8 MiB cap applies to each assembled initialization-plus-media segment, not
the total duration of a stream. Initialization is capped at 1 MiB. There is no
file access, network access, kernel mapping or unbounded input queue in this
component. It deliberately accepts only a limited fragmented MP4 box ordering;
FFmpeg still validates actual codec/container contents. This is not a complete
implementation of the MSE byte stream or SourceBuffer algorithms.

Owned test media contains a real AVC initialization segment and two separately
appended fragments with `tfdt` timestamps. Tests decode ten H.264 frames through
the actual native decoder, verifying red/blue pixels, absolute timestamps across
fragment boundaries, split headers/payloads, backpressure, abort, reset, truncated
input, invalid box ordering/sizes, extended headers and a deterministic mutation
corpus. The same fixture passes in DihOS ARM64 EL0, including process cleanup.
Host ASan/UBSan instrument the assembler, decoder wrapper and test; the existing
reference FFmpeg libraries are not sanitizer-instrumented.

From ARM64 WSL at the repository root:

```sh
python3 tools/webcore_dihos/media/test_fragments.py --native
python3 tools/webcore_dihos/media/record_streaming.py
python3 tools/webcore_dihos/media/record_streaming.py --check
```

Artifacts stay in `build/dihscover-streaming-validation`. No VM image or old
package is modified, and there is no new browser package to install yet.

Remaining integration work:

1. Implement real WebCore `MediaSourcePrivate` / `SourceBufferPrivate` adapters.
   Demux encoded samples and track metadata, preserving rational decode and
   presentation timestamps, append windows, random-access requirements and
   timestamp offsets. Do not treat appends as independent whole videos.
2. Connect bounded encoded-sample queues to persistent decoders and the existing
   player clock; handle buffering, eviction, seek, abort, end-of-stream and
   detached/replaced players. Track audio timing even while output is muted.
3. Enable MSE in a separate, consistently rebuilt WebCore configuration and
   execute JavaScript append/play/seek/cancellation fixtures before advertising
   supported codec/container combinations.
4. Check actual YouTube script/bootstrap and player errors in the guest, including
   media fetches, codecs and unsupported Web APIs. A working MSE fixture alone
   does not establish YouTube or Shorts compatibility.
5. Validate playback and repeated Shorts navigation on Hyper-V, then package the
   matched frontend/renderer/kernel. Audio output remains outside this milestone.

WebKit's [MSE architecture documentation](https://docs.webkit.org/Deep%20Dive/Modules/MediaSourceExtensions.html)
describes the separation between initialization segments, encoded samples,
track buffers and platform playback backends used for this integration.
