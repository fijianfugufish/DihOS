# Native WebKit platform policy

The experimental JSC-only port supplies these contracts through DihOS, without
claiming Unix capabilities:

- WebKit's main thread is the native, generation-tagged thread that first
  initializes it. Repeated initialization on that thread is allowed; changing
  the initializing thread terminates the isolated app. Publication uses native
  atomic operations, and workers compare their own SACX thread identity.
- SACX VM ABI v1 has 4 KiB pages, so page-size discovery returns 4096. It does
  not query the build host or assume the firmware's mapping granule.
- With no OS locale setting yet, browser language defaults to `en-US` and ICU
  search/text boundaries use `en_US`. Calendar conversion remains UTC. These
  are explicit product defaults, not detected host preferences.
- The remaining `RandomDevice` interface uses the same bounded, kernel-backed
  entropy service as the bmalloc and WTF random adapters. It opens no device
  file and fails the app when trusted entropy is unavailable.
- Both WTF process-termination paths terminate the entire isolated process
  through SACX, including its workers. They do not run arbitrary user callbacks
  while other threads might hold locks. This is not a new general-purpose C
  `exit()` implementation or a POSIX process API.
- Diagnostic error strings retain the native numeric error. Single UTF-16
  diagnostic units are encoded as UTF-8; isolated surrogates become U+FFFD.

The resource provider uses the versioned SACX `RESOURCES` operation (24),
requiring both clock and memory grants. Its 80-byte result reports only the
calling process and thread. CPU counters measure active guest slices, including
entry/return overhead, and synchronous service-handler work. Sleeping, parked
waits and gaps between scheduler polls are excluded. This is guest accounting:
host steal time inside a measured interval cannot currently be distinguished,
and general IRQ/runtime/exit-cleanup work is not comprehensively attributed.

Private memory includes the image, live stack/TLS backing, private translation
tables and eagerly owned heap/guard pages. Reserved and committed heap bytes
are separate; committed storage can have no current access permissions. Since
decommit retains physical backing in this VM implementation, it reduces the
committed count but not private backing. Release reduces both reservation and
physical backing. The heap limit is the process's heap budget, not host RAM.

`CPUTime` and memory-footprint hooks use these real counters; unavailable data
does not become a fabricated zero. Remaining runtime imports and final native
WTF/JSC linkage are separate gates. Unix descriptor helpers are omitted from
this no-file profile; this does not provide a POSIX descriptor namespace.

The source inventory uses actual CMake target commands for native adapters:

```sh
python3 tools/jsc_dihos_probe.py --inventory --icu build/icu-dihos/stage-report.json --build-target WTF --build-timeout 600
```

The expanded adapter fixtures test main/worker identity, repeated initialization,
locale IDs, page size and native RandomDevice output alongside synchronization
and entropy checks. Their link/run evidence is recorded separately from source
compilation. Full engine execution and Hyper-V/native hardware validation remain
separate acceptance gates; this code does not update the shipping browser.
`--build-target WTF` checks the support library independently. Even a successful
support-library build leaves `engine_built=false`.

The [native library result](browser-evaluation/native-wtf-library-20261030.json)
records a complete WTF build and ten native source checks with actual CMake
flags. The [ARM64 EL0 result](browser-evaluation/native-webkit-platform-20261030.json)
records all 30 integration groups passing with the expanded adapter binaries.
It covers main/worker identity, locale IDs, page size, CPU/memory getters,
unseeded/seeded RandomDevice behaviour and repeated cleanup. It does not claim
full WTF Thread-object or JavaScriptCore execution coverage.

Native EL0 applications can include `sacx_user_resources.h` and call
`sacx_user_resources_get(&usage)`. Check the result before reading fields; the
operation can reject missing grants, invalid buffers, size/flag mismatches or
unavailable timing. The existing syscall envelope remains ABI version 1.
