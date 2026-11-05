# Native virtual memory for the WebKit port

The interpreter-only allocator adapters now use SACX memory services directly.
They do not expose POSIX `mmap`, raw kernel pointers, executable allocations or
fixed-address replacement mappings. Hyper-V and native ARM64 use the same
userspace code and kernel ownership checks.

## Kernel and SDK contract

ABI v1 retains `SACX_USER_MEMORY_DECOMMIT` at operation 16 and appends sparse
reserve/release at operations 25/26; earlier numbers and structures are
unchanged. QUERY advertises mask `0x7ffffff`.
The existing memory grant is required. A decommit validates ownership, page
alignment, length, commitment and zero flags before revoking access. It clears
commit accounting after publishing the changed mappings. Recommit zeroes the
previous contents before exposing the pages again. Mixed committed/uncommitted
kernel requests fail before revoking any page.

The SDK adapter in `virtual_memory.cpp` tracks its own reservations under a
userspace mutex. That bookkeeping does not confer kernel authority. It provides
aligned, optionally guarded windows inside owned reservations, and splits
commit/protect/decommit operations into at most 64 KiB per kernel call.
Repeated adapter decommit is idempotent; the underlying kernel operation remains
strict. Commit on an already committed page changes protection and preserves
contents. Explicit zero/discard resets contents, including read-only/no-access
pages, through decommit and recommit rather than a privileged writable alias.

The migrated adapter has 16 sparse region slots, a 4 GiB maximum region and
power-of-two alignment up to 4 GiB. Kernel sparse slots are separate from the
legacy allocator's slots. Reservation acquires no payload backing; explicit
commit supplies zeroed pages, and decommit returns them. This is explicit
commitment, not automatic fault-driven demand paging. The 128 MiB development
process physical quota still applies. SDK page-state bookkeeping is allocated
only for touched 1 MiB spans and reclaimed when those spans become empty.
Release issues bounded kernel steps until complete; user execution remains
timer-preemptible. Advisory `hintMemoryNotNeededSoon` preserves data.

## WebKit integration

The pinned source-edit manifest adds a DihOS bmalloc platform branch, replacing
the POSIX VM helpers with `bmalloc-vm.h`. Aligned allocation takes the native
reservation path directly, avoiding POSIX partial-unmap trimming. Native release
requires the exact window and size, and retains the original raw reservation
for correct cleanup. `OSAllocatorDihOS.cpp` implements WTF's corresponding
reservation, commitment, protection, zero and release interfaces. Non-native
ports keep their existing implementations.

Native entropy, threads, clocks and the C/C++ runtime have since allowed actual
[JavaScriptCore execution](dihscover-user-javascriptcore.md). The initial
engine used an 8 MiB structure-heap override. The sparse adapter removes that
memory-model limitation; it does not add DOM or website support to NetSurf.

## Validation and commands

The initial implementation's actual pinned bmalloc header and native WTF implementation linked into a
41,336-byte static ELF with no unresolved imports, SHA-256
`47281f8ab5e2a14aea9947bd853217faf9e909202151ec28b3f36593444df993`.
Four ARM64 EL0 QEMU launches pass alignment, revoke/restore, discard/recommit,
zeroing, protection, unsupported executable/fixed mappings and cleanup checks.
The separate SDK fixture passes concurrent allocations, slot exhaustion,
guard/decommitted/read-only hardware fault isolation and repeated cleanup.
Kernel tests also cover bitmap-word boundaries, zeroing and transactional
rejection. The SACX routing/capability host test and prior runtime/RTTI/large
image suite pass. These are functional tests; native hardware and Hyper-V have
not run these new fixtures yet.

[Retained evidence](browser-evaluation/native-vm-20261029.json) records the exact
ELF and source hashes and the passing QEMU log hash.

From the repository in ARM64 WSL, after preparing the pinned dependencies:

```sh
python3 tools/jsc_dihos_probe.py --inventory --icu build/icu-dihos/stage-report.json
python3 tools/build_webkit_vm_test.py
```

Then from PowerShell:

```powershell
.\tools\test_user_runtime_qemu.ps1 -WebkitVm -Python <python-executable>
```

Add `-Rtti` when that earlier fixture is also built. The standard SDK VM tests
are included by default. Success lines are `PASS: native VM alignment/guards`
and `PASS: actual pinned bmalloc and native WTF OSAllocator adapters`.
SDK fixture exit status is `0x5a`; the actual WebKit adapter fixture is `0x5b`.

The development snapshot/packaging recipe includes the SDK VM sources and four
fixtures. The experimental WebKit fixture is optional and stays under
`build/user-webkit-vm`. No VHDX or working-tree kernel binary was updated.
Deploy a newly compiled kernel before running any new decommit-using fixture
in Hyper-V; old kernels correctly reject the new operation.
