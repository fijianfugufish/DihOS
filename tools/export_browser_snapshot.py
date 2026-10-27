"""Export the published Hyper-V baseline plus the explicit browser overlay.

Never changes the index or copies unrelated GPU/ripple/Wi-Fi working changes.
The fresh temporary directory deliberately excludes the tracked kernel binary.
"""
from pathlib import Path,PurePosixPath
import hashlib,io,json,shutil,subprocess,tempfile,zipfile
root=Path(__file__).resolve().parents[1]
base='c8812d2'
base=subprocess.run(['git','rev-parse',base],cwd=root,check=True,capture_output=True,text=True).stdout.strip()
snapshot=Path(tempfile.mkdtemp(prefix='dihos-browser-'))
archive=subprocess.run(['git','archive','--format=zip',base],cwd=root,check=True,capture_output=True).stdout
with zipfile.ZipFile(io.BytesIO(archive)) as z:
    for entry in z.infolist():
        relative=PurePosixPath(entry.filename)
        if relative.is_absolute() or '..' in relative.parts:raise ValueError('Unsafe archive path')
    z.extractall(snapshot)
paths=['.gitattributes','build_sacx_app.ps1',
 'kernel/apps/sacx_runtime.cpp','kernel/apps/sacx_api_guards.inc','kernel/apps/sacx_app_checkpoint.inc',
 'kernel/apps/sacx_network_service.inc','kernel/apps/sacx_network_start.inc','kernel/apps/sacx_network_lifetime.inc',
 'kernel/apps/sacx_launch_service.inc','kernel/apps/sacx_launch_start.inc',
 'kernel/apps/sacx_launch_read.inc','kernel/apps/sacx_launch_checkpoint.inc',
 'kernel/apps/sacx_cursor_service.inc','kernel/apps/sacx_panic_report.inc',
 'kernel/asm/aa64/aa64_exceptions.c','kernel/hyperv/hyperv_video.c','kernel/include/hyperv/hyperv_video.h',
 'kernel/include/apps/sacx_api.h','kernel/include/apps/sacx_runtime.h','kernel/include/apps/sacx_network_internal.h',
 'kernel/include/system/kfiber.h','kernel/system/kfiber.c','kernel/include/net/http_stream.h','kernel/net/http_stream.c',
 'kernel/include/net/knet_link.h','kernel/net/knet_link.c','kernel/include/net/knet_usb.h','kernel/net/knet_usb.c','kernel/net/knet_receive.inc',
 'kernel/kernel.c','kernel/hyperv/hyperv_net.c','kernel/hyperv/hyperv_diag.c','kernel/usb/usb_ethernet.c',
 'sdk/sacx/include/sacx_api.h','sdk/sacx/freestanding/include/limits.h','sdk/sacx/freestanding/include/time.h',
 'sdk/sacx/apps/dihscover','third_party/freetype','third_party/harfbuzz','OS/System/Fonts/Web',
 'third_party/netsurf-all-3.11/netsurf/content/handlers/html/html.c',
 'third_party/netsurf-all-3.11/netsurf/content/handlers/html/layout.c',
 'third_party/netsurf-all-3.11/netsurf/content/handlers/css/css.c',
 'third_party/netsurf-all-3.11/libcss/src/select/select.c',
 'tools/hyperv.ps1','tools/test_hyperv_package.ps1','tools/hyperv_protocol_tests.c','tools/hyperv_video_tests.c',
 'tools/kfiber_tests.c','tools/http_stream_tests.c','tools/sacx_network_service_tests.cpp',
 'tools/sacx_api_guard_targets.json','tools/browser-dependencies.lock.json','tools/browser_fixtures',
 'tools/sacx_build_cache.ps1','tools/test_sacx_build_cache.ps1',
 'docs/browser-evaluation','docs/dihscover-responsive.md']
for pattern in ('tools/browser_*.cpp','tools/browser_*.c','tools/browser_*.py','tools/browser_*.cjs',
                'tools/test_browser*.ps1','tools/generate_*browser*.py','tools/generate_sacx_api_guards.py',
                'tools/verify_browser_dependencies.py','tools/export_browser_snapshot.py','tools/build_browser_validation.ps1'):
    paths.extend(p.relative_to(root).as_posix() for p in root.glob(pattern))
files=set()
for relative in paths:
    source=root/relative
    if not source.exists():raise FileNotFoundError(source)
    if source.is_dir():files.update(p for p in source.rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    else:files.add(source)
manifest={};tree=hashlib.sha256()
for source in sorted(files):
    relative=source.relative_to(root);target=snapshot/relative;target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,target);sha=hashlib.sha256(source.read_bytes()).hexdigest();manifest[relative.as_posix()]=sha
    tree.update(relative.as_posix().encode()+b'\0'+bytes.fromhex(sha))
for relative in ('OS/aa64/KERNEL.ELF','OS/x64/KERNEL.ELF'):
    target=snapshot/relative
    if target.exists():target.unlink()
ignore=snapshot/'.gitignore';ignore.write_text(ignore.read_text()+'\n!tools/test_browser.ps1\n!tools/test_browser_fonts.ps1\n!tools/build_browser_validation.ps1\n')
record={'base':base,'overlay_sha256':tree.hexdigest(),'files':manifest}
(snapshot/'browser-source-manifest.json').write_text(json.dumps(record,indent=2))
print(json.dumps({'path':str(snapshot),'base':base,'overlay_sha256':tree.hexdigest(),'files':len(files)}))
