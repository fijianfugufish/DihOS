# Usable ARM64 Dihscover checkpoint

On 5 November 2026, the user reported that Dihscover loaded YouTube, searched,
opened a watch page and played video inside ARM64 Hyper-V. Playback was roughly
3–5 FPS with visibly poor quality, and page interaction remained slow. This is
an experimental usable checkpoint, not a claim of smooth playback or general
website compatibility.

The checkpoint includes the preemptible EL0 runtime, checked SACX bridges,
threading/heap support, JavaScriptCore/WebCore port, packaged fonts, asynchronous
network resources, software painting, MSE/H.264 playback, memory-pressure fixes
and skipped visual view transitions. The latter reproduces the original layer
exit in a native control and preserves real callbacks, promise results and pixels.

Validation records are under `docs/browser-evaluation/`. ARM64 QEMU exercises
actual engine/runtime code with modeled networking; it does not establish live
site performance. The live YouTube result above comes from the user's VM test.
The packaged milestone was `dihscover-software-transitions-20261105`.

The commit snapshot was also built separately from a HEAD archive plus selected
sources. ARM64 boot/stage2/kernel compilation, Hyper-V host tests and browser
host tests passed. `browser-evaluation/dihscover-usable-checkpoint-20261105.json`
records the source and binary hashes and the limits of that validation.

Public source defaults to the pinned Mozilla trust bundle. School/inspection
certificates remain an explicitly selected local build option; the public
`dihos_tls_extra_ca.h` contains no additional trust anchor.

Kernel/source additions are selected from the tested source snapshots. Native
GPU, desktop-ripple, Wi-Fi work, generated disk images, build caches, logs and
the working-tree kernel binary are outside this checkpoint.

Performance investigation and next milestones are documented in
`dihscover-performance.md`. Audio output remains deferred.
