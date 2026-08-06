# SimpleWebP Pin

- Upstream: https://github.com/MikuAuahDark/simplewebp
- Commit: `d1a728a1f8ec7348ca2a5039b6dd813b83986fbb`
- Version macro: `20260718`
- License: BSD-3-Clause (`LICENSE.md`) with the included WebM patent grant (`PATENTS.md`)

DIHOS uses the decoder only, disables stdio, supplies page-backed allocation,
rejects animated/oversized inputs, and caps decoded images at 4096 x 4096 and
64 MiB.
