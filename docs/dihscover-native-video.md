# Dihscover native video work

The shipping browser still has `ENABLE_VIDEO=0` and `ENABLE_MEDIA_SOURCE=0`.
The work below is a decoder foundation, not a working YouTube player. A
scrollable YouTube loading skeleton does not establish that its application
JavaScript or resource loading has completed.

## Implemented

`tools/webcore_dihos/build_media_decoder.py` builds FFmpeg 9.0.2 for
`aarch64-none-elf` using the existing DihOS C ABI. The official source archive
is pinned by SHA-256 and verified with the FFmpeg release signature. Extracted
source bytes are checked against that archive. The build is separate from
the existing WebCore archives and packages.

This profile contains MP4/MOV and Matroska/WebM demuxers, software H.264,
VP8, VP9 and MPEG-4 decoders, and BGRA colour conversion. It excludes network
and file protocols, devices, encoders, external codec libraries and GPL/nonfree
features. The always-built optional file helpers explicitly deny access.
The decoder must have one serialized owner; FFmpeg threading is disabled.

`media/memory_video.c` owns a copy of its input and keeps all FFmpeg objects
private. It validates dimensions, uses checked seek arithmetic, disables
nested URL access, limits individual allocations, advances at most 16 packets
per call, preserves timestamps and releases buffers on close/error. Current
limits are an 8 MiB complete input and a 1920×1080 displayed frame. H.264's
1088 coded rows with an SPS crop to 1080 are permitted. This API is intended
for initial progressive-file integration; it is **not** a streaming or MSE API.

Scoped native C helpers provide missing string/search/log operations and the
numeric UTC timestamp format used by FFmpeg. Decoder seeds use SACX entropy,
never the host clock or process addresses. No new kernel syscalls are added.

## Validation commands (ARM64 WSL, repository root)

Download `ffmpeg-9.0.2.tar.xz`, its `.asc` signature and `ffmpeg-devel.asc` from
the official FFmpeg download site into `build/dihscover-media-validation`.
Import the release key into that directory's `gnupg` home. The builder requires
fingerprint `FCF986EA15E6E293A5644F10B4322F04D67658D8` and archive SHA-256
`8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`.

```sh
python3 tools/webcore_dihos/media/make_fixture.py build/dihscover-media-validation/red-blue.mp4
python3 tools/webcore_dihos/build_media_decoder.py
python3 tools/webcore_dihos/media/build_reference.py
python3 tools/webcore_dihos/media/build_fixture.py
python3 tools/webcore_dihos/media/test_native.py
python3 tools/webcore_dihos/media/record_validation.py
```

The host reference checks independently generated, owned H.264/MP4 fixtures.
Its executable/libraries are never packaged or linked into the native target.
Host ASan/UBSan cover the adapter and fixture; the host FFmpeg archives are not
sanitizer-instrumented. The ARM64 test runs the actual native decoder in EL0
under the production page-table, scheduler and syscall implementation, then
checks that all process pages are reclaimed. This is QEMU execution, not a
Hyper-V or bare-metal playback/performance measurement. Codec correctness
tests currently cover H.264 intra/PCM samples, not the VP8/VP9/MPEG-4 decoders
or all H.264 profiles and prediction modes.

## Remaining browser integration

1. Build a separate, consistently video-enabled WebCore configuration. Header
   layouts and all affected archives must match; do not mix video-enabled
   objects into the shipping disabled-video archives.
2. Implement a concrete media strategy/player using WebCore's media resource
   loading, existing verified TLS/cookies/CORS and the CPU compositor. Expose
   truthful codec support and attach timing, pause, seek, ended/error events
   and invalidation to the existing Dihscover UI. Decoder work needs a bounded
   owner and safe cancellation; no synchronous network calls in callbacks.
3. Verify muted progressive playback, navigation/close during decoding,
   multiple elements, memory exhaustion and repeated restarts in Hyper-V.
4. Add range-backed input and MediaSource/SourceBuffer support: initialization
   segments, fragmented MP4/WebM, timestamp offsets, append/removal, backpressure,
   seek and end-of-stream. Do not raise the complete-input bound and call it
   streaming. Audio tracks may be demuxed/ignored initially, but capability
   reporting must not promise an audio decoder or output that is absent.
5. Diagnose the YouTube bootstrap/resource failures and test actual Shorts
   playback/scrolling. A codec test alone does not prove site compatibility.

Keep every frame and decode operation inside DihOS. No host browser, remote
rendering service, certificate bypass or site-specific video URL scraper is
part of this design. Any eventual static-library release must include the
FFmpeg licence/source and relinking materials alongside its build recipe.
