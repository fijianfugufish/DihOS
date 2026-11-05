# Dihscover scrolling, resizing and frame uploads

This update keeps Dihscover's existing frontend and changes the embedded
renderer and its surface transport.

- Paint through WebCore's scroll view, including its scroll translation and
  visible clipping, rather than painting unscrolled document coordinates.
- Enable normal scrollbars and run WebCore's rendering-update/finalization
  lifecycle, including resize, scroll, media-query and animation callbacks.
- Accept page surfaces up to 1920×1080, removing the old 1024×768 clamp. Retry
  a rejected resize instead of recording its dimensions as already delivered.
- Upload up to 64 KiB per syscall while checking and copying in bounded 4 KiB
  pieces. Check every piece before mutation and keep full-frame presentation
  atomic. A 960×568 page needs 34 upload calls rather than 533.
- Compare painted pixels with the last presented frame and skip identical
  uploads. Scheduled rendering updates use WebCore's preferred cadence; the
  fallback refresh remains available for unscheduled changes.

The host tests cover later-page rejection within a large upload, atomic
presentation, protected buffers, ownership, foreground input and viewport
dimensions above the previous limit. The ARM64 renderer regression adds a tall
document, visible wheel movement, fixed-position pixels, a responsive CSS media
query and a JavaScript resize handler at a 1280-pixel window width.
QEMU networking is modeled; actual Hyper-V scroll feel and frame rate still
need a guest smoke test. The reduction in syscall count is not an FPS claim.

With the VM off, update from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-scroll-resize-20261102
```

Start the VM and open Dihscover normally. Build and test commands remain in
`dihscover-webcore-integration.md`. The response-size, bitmap, cookie and media
limitations of the preceding milestone remain.
