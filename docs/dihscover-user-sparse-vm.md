# Sparse ARM64 userspace virtual memory

The new kernel service reserves large virtual ranges without allocating their
payload RAM. Commit allocates zeroed, process-owned backing; decommit returns
that backing, unused metadata and empty page tables. The original eager memory
service, malloc and browser VM adapter are retained for compatibility. Moving
the browser adapter to this service is the next step, followed by rebuilding
its verified target dependencies. This milestone does not provide WebCore.

## ABI and ownership

SACX ABI version 1 gains two appended operations, advertised by QUERY's
`0x7ffffff` operations bitmap. Both require the existing MEMORY capability:

| Operation | Arguments | Result |
| --- | --- | --- |
| 25, SPARSE_RESERVE | bytes, alignment (zero means 4096), guard pages per side | Owned virtual address or negative error |
| 26, SPARSE_RELEASE | exact returned address, 0, 0 | 0 when complete; -11 while more release work remains |

Use the existing COMMIT, PROTECT and DECOMMIT operations on sparse addresses.
Each handles at most 64 KiB. Pages are 4096 bytes; alignment must be a power of
two from 4096 through 4 GiB. Each process has 16 sparse reservation slots and a
16 GiB private arena; each reservation is at most 4 GiB with up to 16 guard
pages per side. Virtual capacity is separate from the unchanged physical
process quota. Metadata is charged along with committed backing.

Reservations initially grant no access. Commit is transactional and zeroes
new pages. Recommit after decommit also zeroes data. PROTECT supports read,
read/write and no access; executable heap permissions are rejected. A pending
release retains its virtual span and rejects mutations with -16. It revokes
at most 64 KiB of payload per call. Callers must synchronize their threads
before release. Fault/exit/cancellation cleanup also owns all sparse backing.

`sdk/sacx/include/sacx_user_sparse_vm.h` supplies reserve, release-step and
complete-release wrappers. The latter repeats the bounded syscall while -11
is returned. The kernel/desktop scheduler regains control at every syscall.

The arena is claimed only in an inactive, private process map. Its identity-VA
window must fall outside every firmware memory descriptor, boot allocation,
known physical-to-virtual alias and timer/GIC register window. Existing mapped
leaves must be EL1-only normal identity mappings; aliases/device leaves are
rejected. Claiming the arena changes private descriptors, preserving the
active kernel/firmware map. Unsupported layouts fail closed. No new TCR layout
or unchecked kernel mapping is installed.

## Validation

The [four-level result](browser-evaluation/sparse-vm-arm64-l0-20261031.json)
passes 39 ARM64 integration groups, including the existing JavaScriptCore
suite and busy-engine cancellation. These engine binaries intentionally remain
the previously tested ABI-v1 binaries; this is a compatibility test, not a new
engine/ICU build. The [three-level result](browser-evaluation/sparse-vm-arm64-l1-20261031.json)
passes 28 base integration groups. Exact sparse fixture ELF bytes were checked
against their bytes embedded in each passing kernel.

Coverage includes GiB-aligned reservations, sparse distant pages, accounting,
zeroing, read-only/decommit/guard/NX hardware faults, bounded release, closing
state, slot/quota exhaustion and resident cancellation. Kernel tests force
fragmented physical backing, inject 19 metadata/payload/page-table allocation
failures, verify rollback and table reclamation, and isolate two processes at
the same virtual address. A fully identity-mapped 16 GiB firmware window uses
2 MiB leaves with contiguous hints. Host tests cover firmware/boot/alias
exclusion and denied SACX capabilities. The
[user-reported Hyper-V success fixture](browser-evaluation/sparse-vm-hyperv-20261031.json)
also passed: exit 0x65, the expected success message, 60 syscalls, zero remaining
heap reservations/commitments and 72 KiB peak committed payload. Physical
hardware and the other guest fault fixtures remain unverified.

Run base tests with the installed Python interpreter:

```powershell
.\tools\test_user_runtime_qemu.ps1 -Python python
python tools/record_sparse_vm_test.py --variant l1
```

For the four-level compatibility gate, add `-FirmwareL0 -FirmwareL2 -WebkitSync
-WebkitRandom -WebkitVm -Rtti -FeatureMain -Libm -JscSupport -Stdio -Jsc
-JscSpin -JscFramePaced`, then record with `--variant l0`. `-ReusePrograms` is
allowed only when its source/dependency/compiler/output checks pass.

## Prepared Hyper-V package

Package: `build/dihos-user-sparse-20261031`, build
`20261031T093654Z-4424B0C3E5D2`. It contains the new isolated kernel and six
sparse fixtures, retaining the known working browser and standalone JSC
binaries/resources. Source overlays and validation reports are under
`build/user-validation-sparse-20261031`; the checkout's working kernel binary
was preserved. Manifest hashes and package checks pass.

With the VM off, run from the repository in Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-sparse-20261031
```

After booting:

```text
proc:run 0:/OS/System/Programs/UserRuntime/sparse.elf
proc:status
```

Expected: `exited`, status `0x65`, the sparse-VM success message, and zero
remaining reserved/committed heap bytes. This reserves GiB-sized address
ranges while touching only a small amount of RAM. Optional `sparse_spin.elf`
checks desktop responsiveness with resident sparse pages; cancel it using
`proc:stop`. Other sparse fixture files intentionally fault to test isolation.

To verify the prepared package against its preserved full compatibility log:

```powershell
python tools/verify_user_package.py --package build/dihos-user-sparse-20261031 --validation build/user-validation-sparse-20261031 --qemu-log build/user-validation-sparse-20261031/qemu-final.log
```
