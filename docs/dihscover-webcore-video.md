# WebCore video player integration

Muted MP4/H.264 playback passes native ARM64 EL0 execution in the QEMU
kernel harness. The owned HTML video fixture checks seeking, painted frames,
timed playback, ended events and cleanup. Existing browser regression tests
also pass with video enabled. Hyper-V performance and live media remain
unvalidated. The native decoder tests are recorded in
[the decoder notes](dihscover-native-video.md).

`tools/webcore_dihos/media/configure_engine.py` creates a separate WebCore
2.54.2 source/build pair with `ENABLE_VIDEO=1`. MediaSource, encrypted media,
Web Audio, capture and hardware compositing remain disabled. The player has
its own DihOS engine identifier and uses the existing software compositor.

The backend uses `MediaPlayer::mediaResourceLoader()`, not FFmpeg's URL
protocols. WebCore retains request credentials, redirects, cookies, origin
checks and canvas tainting. Request clients hold generation-checked weak
references; replacement/cancellation detaches them and releases the decoder.

Initial support is muted MP4 containing H.264 video. A complete input of at
most 8 MiB is buffered before decoding starts. This does not implement
incremental streaming. Capability probes reject MediaSource, unsupported
containers and unsupported audio codec lists. Audio packets in an otherwise
accepted MP4 are ignored. The player supports timed CPU frame presentation,
pause, seek, end-of-playback notification, playback rates from 0 to 4, and
bounded catch-up that drops stale frames. Allocated frames have owned Cairo
storage, so replacing/closing a decoder cannot invalidate an image in use.

All FFmpeg operations run serially on the renderer's main thread in
preemptible EL0. There are no synchronous network waits or sleeps in media
callbacks. Decode calls advance bounded packets; catch-up yields the event
loop. Long individual decode/open operations still need performance and
cancellation testing on Hyper-V.

## Build and test (ARM64 WSL, repository root)

```sh
python3 tools/webcore_dihos/media/configure_engine.py --build --jobs 8
python3 tools/webcore_dihos/media/build_renderer.py --resume
python3 tools/webcore_dihos/media/test_page.py
```

`--player-only` on the engine builder checks the backend translation unit.
`--compile-only` on the renderer builder permits compilation while engine
archives are still building; `--fixture-only` checks just the playback fixture.
`--resume` reuses objects only when their command, compiler binary, object and
every compiler-reported dependency still match their recorded hashes.

The Page fixture embeds owned MP4 test data. It checks `<video>` capability
probes, dimensions/duration, seeking to a blue frame, actual painted pixels,
red-to-blue timed playback and the ended event. The kernel harness then checks
process cleanup. The renderer must pass that execution test before playback
is described as working; compiler success alone is insufficient.

Run `test_regressions.py` and `test_elf_limits.py` in the same directory for
browser regression and sanitizer checks. In Windows, run
`tools/webcore_dihos/media/package_player.py`, then repeat with `--check`.
Packaging preserves the previous package resources and the isolated Hyper-V
kernel, raising only its validated relative-relocation ceiling to 524288.
The 128 MiB image limit and 256-fixup service step remain unchanged.

With the VM turned off, Administrator PowerShell can install the package:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-video-player-20261104
```

MediaSource/range-backed buffering and actual YouTube bootstrap/Shorts tests
remain subsequent requirements. Direct MP4 URL navigation is not provided;
this backend handles HTML video elements. Audio output is unsupported.
