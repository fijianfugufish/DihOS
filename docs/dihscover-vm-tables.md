# Virtual-memory metadata capacity and failure diagnostics

The guest exited with status 175 (`0xaf`) and the old generic `WTF native virtual memory operation failed` message. That message identifies neither the operation nor its error code: it can come from commit, decommit, release or rollback. The screenshot alone does not establish the cause.

One independently testable capacity problem is the 192-private-page-table ceiling. Scattered mappings can consume this metadata while most of the process's physical byte allowance remains unused. The native regression reserves one sparse range and commits 256 individual pages at 2 MiB intervals, forcing distinct L3 tables with only a small payload. The 192-table control must fail with `-12`, exactly 192 tables and byte quota remaining. The expanded configuration must map all 256 pages and reclaim everything.

The bounded table allowance is now **512 pages**. The loader already reserves its maximum metadata cost inside the existing 256 MiB ordinary / 512 MiB trusted-browser process budgets; those total budgets are unchanged. Heap regions remain at 32, sparse reservation slots at 16, and ownership, guard and NX policies are unchanged. This is a tested capacity correction, not proof that metadata exhaustion caused the specific guest exit.

The freshly linked WTF OSAllocator adapter now reports the failing operation, signed result, address, length, caller and current private/reserved/committed/heap-limit byte counts, in one bounded allocation-free SACX message. Nonfatal `try` operations retain their contracts. A native fixture calls decommit on an aligned but unowned address and must exit 175 with `decommit rc=-14`, the exact range, nonzero accounting and a caller resolving to the fixture. A sanitizer test covers formatting bounds and extreme signed/unsigned values. No memory checks are disabled or failures ignored.

Build a matched isolated kernel in Windows PowerShell:

```powershell
python tools/webcore_dihos/prepare_youtube_kernel.py --vm-tables
.\build\youtube-vm-tables-kernel\src\build.ps1 -ProjectRoot .\build\youtube-vm-tables-kernel\src -ArmOnly -KernelOnly
```

Validate in Ubuntu WSL from the checkout:

```sh
python3 tools/webcore_dihos/validate_optional_trace.py --vm-tables
```

Package/check in Windows PowerShell:

```powershell
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -EngineDirectory build/dihscover-vm-tables-validation/engine -OutputDirectory build/dihscover-vm-tables-validation/frontend
python tools/webcore_dihos/package_optional_trace.py --vm-tables
python tools/webcore_dihos/package_optional_trace.py --vm-tables --check
```

With the VM off:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-vm-tables-20261105
```

The package retains the larger CSS allowance, memory pool and IndexedDB handling fixes and desktop assets. Packaging leaves the VHD and working-tree kernel binary unchanged. If the guest still stops, `proc:status` should retain the specific `WTF VM` failure rather than the previous generic line. Live YouTube layout and playback remain unverified.
