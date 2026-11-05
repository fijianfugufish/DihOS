# Dihscover IndexedDB crash fix

The guest's hardened-optional trace resolves to `IDBConnectionProxy::IDBConnectionProxy`, `IDBConnectionToServer::create`, `EmptyDatabaseProvider`, `Page::idbConnection`, `Document::idbConnectionProxy` and `DOMWindowIndexedDatabase::indexedDB`. The pinned empty-client delegate returns a disengaged connection identifier; the proxy constructor dereferences it. Accessing `window.indexedDB` was sufficient to terminate the renderer.

The owned Page now installs a DihOS database provider. Each provider/session retains a stable generated connection identifier and immediately transitions the connection to WebCore's native unavailable-server state before exposing it. Existing WebCore code completes open/delete requests asynchronously with `UnknownError` and enumeration with an empty list. A generated identifier alone would fix the assertion but leave requests hanging, because the old delegate discards all operations. No hardening checks are disabled, no fake successful database is returned, and no file access is added.

This is graceful handling of unavailable storage, **not an IndexedDB storage implementation**. Sites that require successful IndexedDB writes can still fail. Persistent storage, quotas and database transactions remain future work. Live YouTube bootstrap and playback require guest retesting; passing the local native media fixture does not establish that YouTube works.

The ARM64 fixture accesses the actual JavaScript IndexedDB factory, runs twelve concurrent open/delete requests, checks asynchronous error delivery and request state, checks enumeration, and tears down an iframe with a pending request. It then runs the existing native MediaSource playback fixture. A separate control build deliberately retains the empty provider: the same JavaScript access must reproduce the optional abort and resolve its caller to `IDBConnectionProxy`. General browser, allocator/VM, loader and assertion diagnostics regressions remain enabled.

Build/test in Ubuntu WSL from this checkout:

```sh
python3 tools/webcore_dihos/validate_optional_trace.py --indexeddb
```

Build/package/check in Windows PowerShell:

```powershell
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -EngineDirectory build/dihscover-indexeddb-validation/engine -OutputDirectory build/dihscover-indexeddb-validation/frontend
python tools/webcore_dihos/package_optional_trace.py --indexeddb
python tools/webcore_dihos/package_optional_trace.py --indexeddb --check
```

With the VM off:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-indexeddb-20261104
```

The kernel and desktop assets are inherited unchanged from the preceding verified package. Packaging does not modify the VHD or the working-tree kernel binary. The original failure addresses, relative offsets and resolved functions are recorded in the validation directory's `fault.json`; matching symbols and execution reports are retained with the package proof.
