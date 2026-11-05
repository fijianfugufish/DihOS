# Dihscover fragmented-video player

This candidate connects WebCore's standard JavaScript MediaSource and
SourceBuffer implementations to the DihOS software video player. It uses the
existing Dihscover toolbar and renderer process, on Hyper-V and native ARM64.
No host browser or remote rendering service is involved.

The supported streaming format is fragmented MP4 with H.264 video. A separate
AAC-LC track supplies buffering and timing information; its packets are
discarded and no audio decoding or output is claimed. Capability probes reject
WebM, AV1, encrypted media and unsupported codecs. MediaSource workers remain
disabled. These limits can prevent a website from choosing a usable stream.

Each append is owned until the parser finishes. Parsing yields between bounded
batches. Segments are limited to 8 MiB and initialization data to 1 MiB;
SourceBuffer storage is bounded to 32 MiB. The decoder consumer queues at most
32 packets and 4 MiB, with backpressure into WebCore's track buffers. Decoded
frames are limited to 1920×1080. Frames
are copied into owned software surfaces. Seeking, removal and replacement
invalidate pending callbacks, and closing the renderer releases process pages.

Decoder work runs on the renderer's main event loop in preemptible EL0.
WebCore retains Fetch, origin and canvas security checks. Playback uses a
monotonic clock, buffered-range stall boundaries, rational media endpoints and
final-position notifications when a pause races the presentation timer.

Build and execute from ARM64 Ubuntu WSL at the repository root:

```sh
python3 tools/webcore_dihos/media/configure_mse_player.py --compile --build --jobs 4
python3 tools/webcore_dihos/media/build_renderer.py --mse --resume
python3 tools/webcore_dihos/media/test_mse_page.py
python3 tools/webcore_dihos/media/test_mse_regressions.py
python3 tools/webcore_dihos/media/prepare_mse_frontend.py
```

Then build the existing frontend and seal a fresh package in Windows
PowerShell (the packager refuses to overwrite a sealed candidate):

```powershell
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -EngineDirectory build/dihscover-mse-player/engine -OutputDirectory build/dihscover-mse-player/frontend
python tools/webcore_dihos/media/package_mse_player.py
python tools/webcore_dihos/media/package_mse_player.py --check
```

After reviewing the test results, with the VM turned off, Administrator
PowerShell can update its system files while preserving user files:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-mse-player-20261104
```

The native JavaScript fixture exercises split appends, repeated initialization,
changeType, queue backpressure, origin-clean canvas pixels, seeking, timed
red/blue painting, end-of-stream extension, partial-append abort, pending-seek
cancellation and source replacement. It also checks a separate AAC timing
track whose fractional endpoint extends beyond the video track. The kernel
harness verifies process cleanup. These are owned offline fixtures: passing
them does not demonstrate live YouTube or Shorts playback.

The separate browser regression suite covers the existing CSS, image,
networking and iframe behavior. Loader tests use address/undefined-behavior
sanitizers and validate the actual renderer against the selected kernel's
image and relocation bounds. Neither a VHD update nor a live-site result is
implied by compiling or running these tests.
