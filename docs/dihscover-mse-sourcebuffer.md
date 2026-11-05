# Native SourceBuffer and persistent decoding

This experiment continues the [fragmented MP4 work](dihscover-streaming-video.md).
It does not enable YouTube or MSE in the packaged browser. The working video
package and VHD remain unchanged.

Implemented components:

- `encoded_media.c` extracts owned initialization metadata and encoded AVC/AAC
  samples from MSE-style MP4 inputs. It preserves rational presentation/decode
  times and duration. It accepts one fragment at a time with empty initialization
  sample tables, rejects unsupported/encrypted tracks, and denies nested I/O.
  It compensates for the pinned MOV demuxer's normalization of negative
  composition offsets without depending on FFmpeg private structures.
- `SourceBufferDihOS.cpp` implements the real WebCore `SourceBufferPrivate`
  ingestion interface. Initialization reports actual audio/video tracks and
  copied codec configuration. Encoded samples enter WebCore's common track
  buffers, retaining its timestamp-offset, append-window, eviction and seek
  algorithms. Parsing yields after 16 packets/assembler steps per dispatch.
  Removed buffers cancel pending work, free parser input and flush their consumer.
- `DihOSSampleConsumer` defines the decoder queue boundary with explicit
  readiness, enqueue, flush and one-shot readiness notification operations.
  The player must supply its implementation; this is not yet attached to a
  `MediaSourcePrivate` or a browser media element.
- `sample_decoder.c` keeps one native H.264 decoder alive across fragment
  boundaries. It copies configuration and packet payloads, supports codec
  backpressure, drains only at end-of-stream, flushes for discontinuities and
  produces the existing BGRA frame ABI. It performs no file/network I/O and
  creates no decoder threads. AAC timing is parsed, but AAC decoding and audio
  output are not implemented.

Validation status is deliberately separate:

| Component | Result |
| --- | --- |
| Encoded initialization/configuration and packet ownership | Host wrapper ASan/UBSan and DihOS ARM64 EL0 pass |
| Rational video timestamps, signed CTS offsets and 64-bit `tfdt` | Host and ARM64 EL0 pass |
| AAC-LC metadata and 44100 Hz packet timing | Host and ARM64 EL0 pass; no audio decoding claim |
| Persistent decoder across two fragments, red/blue pixels, timing, backpressure and repeated flush | Host and ARM64 EL0 pass |
| SourceBuffer adapter | Compiles against the pinned MSE-enabled WebCore configuration |
| SourceBuffer through JavaScript, actual player attachment, browser streaming | Not executed yet |
| YouTube/Shorts | Unsupported and unvalidated |

The sanitizer build instruments the new adapters and fixtures, not the existing
reference FFmpeg libraries. Test media is generated locally; the tests do not
fetch or play YouTube content. The native test kernel also verifies process-page
cleanup. These are functional tests, not Hyper-V playback-performance results.

Limits remain explicit: 8 MiB per assembled input, 1 MiB initialization,
8 tracks, 64 KiB codec configuration, 1 MiB per encoded sample, 1920x1080 decoded
frames and a 32 MiB WebCore platform buffer budget. Input assembly and encoded
sample buffering have separate budgets. No unbounded stream-duration download
is introduced.

From ARM64 WSL at the repository root:

```sh
python3 tools/webcore_dihos/media/test_encoded.py --native --decoder
python3 tools/webcore_dihos/media/configure_mse.py --compile
python3 tools/webcore_dihos/media/record_mse.py
python3 tools/webcore_dihos/media/record_mse.py --check
```

The MSE source/build caches are separate from the working file-video engine.
Generated reports and test binaries live in `build/dihscover-mse-engine` and
`build/dihscover-sample-decoder-validation`. The experiment still rejects MSE
capability probes and explicitly fails the MSE load overload; it does not
advertise a backend which cannot yet attach and play.

Next integration gate: implement `MediaSourcePrivate` and the consumer queues,
connect them to the player's monotonic clock and presentation path, then execute
real JavaScript `MediaSource` append/play/seek/abort/end-of-stream fixtures.
Only after that should MSE capabilities be advertised, followed by guest YouTube
bootstrap/player diagnostics and actual Shorts navigation tests.
