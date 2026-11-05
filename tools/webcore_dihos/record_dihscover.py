"""Seal the existing Dihscover UI / isolated WebCore integration milestone."""
from pathlib import Path
import hashlib,json,shutil,zipfile
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/dihscover-integration-validation'
DEST=ROOT/'docs/browser-evaluation/dihscover-webcore-integrated-20261102.json'
def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as stream:
  for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
 return h.hexdigest()
def check(value,message):
 if not value:raise SystemExit(message)
if DEST.exists():raise SystemExit('Retain the previous proof; choose a new milestone')
page=json.loads((ROOT/'build/webcore-headless-page/report.json').read_text())
check(all(page.get(k) for k in ['embedded','navigation','https','linked','loader_accepted']),'Build the embedded renderer first')
for path,key in [('tools/webcore_dihos/build_page_fixture.py','builder_sha256'),('build/webcore-engine-openssl/report.json','engine_report_sha256'),('build/webcore-headless-page/compile-report.json','client_compile_report_sha256')]:
 check(sha(ROOT/path)==page[key],'Changed native build input: '+path)
client=json.loads((ROOT/'build/webcore-headless-page/compile-report.json').read_text())
for path,digest in client['navigation_inputs'].items():check(sha(ROOT/path)==digest,'Changed embedded renderer input: '+path)
check(any(p.endswith('/browser_embedded.cpp') for p in client['support_sources']),'Embedded renderer not compiled')
check(not any(p.endswith('/navigation_page.cpp') for p in client['support_sources']),'Preview toolbar was linked')
check(sha(ROOT/'build/webcore-headless-page/page.elf')==page['elf_sha256'],'Changed native renderer')
metadata_path=ROOT/'build/sacx/Dihscover-engine.json';metadata=json.loads(metadata_path.read_text(encoding='utf-8-sig'))
check(metadata['engine']=='WebCore','Build the normal Dihscover WebCore frontend')
for path in ['build/sacx/Dihscover.sacx','build/sacx/Dihscover-WebCore.sacx']:
 check(sha(ROOT/path)==metadata['frontend_sha256'],'Changed frontend artifact')
check(sha(ROOT/'build/sacx/Dihscover-WebCore.bin')==metadata['engine_sha256']==page['elf_sha256'],'Frontend/renderer pairing mismatch')
for path,digest in metadata['sources'].items():check(sha(ROOT/path)==digest,'Changed frontend source: '+path)
check(sha(ROOT/'build/webcore-headless-page/report.json')==metadata['native_report_sha256'],'Native report changed')
qemu=ROOT/'build/user-runtime/qemu.log';log=qemu.read_text()
check('PASS: embedded WebCore uses the existing-UI command bridge, resizes, navigates, edits forms, preserves DOM cancellation, traverses history and closes cleanly' in log,'Embedded ARM64 regression incomplete')
check('PASS: sparse quota/slot bounds' in log and 'FAIL:' not in log and 'FATAL' not in log,'ARM64 regression failed')
check('PASS: Dihscover existing frontend, isolated renderer bridge, ownership, lifecycle, ABI and loader checks' in (OUT/'host-tests.log').read_text(),'Host integration checks missing')
check('PASS: browser controls and focused owned-window keyboard delivery' in (OUT/'window-tests.log').read_text(),'Window regression missing')
check('Built SACX app:' in (OUT/'frontend-build.log').read_text(),'Frontend build incomplete')
snapshot=json.loads((OUT/'source.json').read_text())
for path,digest in snapshot['files'].items():check(sha(OUT/'source'/path)==digest,'Changed isolated source: '+path)
kernel=OUT/'source/OS/aa64/KERNEL.ELF';check(kernel.is_file(),'Build the isolated desktop kernel')
check('Built:' in (OUT/'kernel-build.log').read_text() and 'Copy to USB:' in (OUT/'kernel-build.log').read_text(),'Isolated desktop build incomplete')
check(sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d','Working kernel changed')
# Inherit only the unchanged transport/crypto verification, never UI success.
previous=json.loads((ROOT/'docs/browser-evaluation/native-controls-20261102.json').read_text())
previous_snapshot=json.loads((ROOT/'build/native-controls-validation/source.json').read_text())
tls_sources={}
for path in ['kernel/net/knet_usb.c','kernel/include/net/knet_usb.h','kernel/include/net/tls_verify.h','kernel/net/tls_roots.inc','kernel/apps/sacx_user_network.inc','kernel/include/apps/sacx_network_internal.h']:
 check(sha(ROOT/path)==previous['source_hashes'][path],'TLS/network source changed; revalidate it: '+path)
 tls_sources[path]=sha(ROOT/path)
path='kernel/apps/sacx_network_service.inc';check(sha(ROOT/path)==previous_snapshot['files'][path],'Transport scheduler changed');tls_sources[path]=sha(ROOT/path)
sources={}
for directory in [ROOT/'tools/webcore_dihos',ROOT/'sdk/sacx/apps/dihscover']:
 for path in directory.rglob('*'):
  if path.is_file() and path.suffix in ['.h','.cpp','.c','.inc','.py','.json','.ld','.ps1','.md']:sources[path.relative_to(ROOT).as_posix()]=sha(path)
for path in snapshot['files']:sources[path]=sha(OUT/'source'/path)
# Only entries whose root checkout corresponds to the exported overlay belong
# to the root source list. The reconstructed shell is retained in the zip.
sources={path:digest for path,digest in sources.items() if (ROOT/path).is_file() and sha(ROOT/path)==digest}
for path in ['tools/export_user_snapshot.py','build_sacx_app.ps1','tools/sacx_pack.cpp','tools/hyperv.ps1',
 'sdk/sacx/include/sacx_api.h','sdk/sacx/include/sacx_keys.h','sdk/sacx/include/sacx_window_keys.h','sdk/sacx/imports/default.imports.txt',
 'tools/browser_bridge_tests.c','tools/browser_session_lifecycle_tests.cpp','tools/browser_existing_ui_tests.cpp','tools/browser_api_layout_tests.cpp','tools/browser_api_layout_kernel.c',
 'tools/user_loader_tests.c','tools/user_sacx_tests.c','tools/user_window_tests.c','tools/user_window_desktop_tests.cpp','tools/test_dihscover_integration.ps1','tools/test_browser_controls.ps1',
 'tools/embed_user_programs.py','tools/test_user_runtime_qemu.ps1','tools/user_runtime_qemu_tests.c','docs/dihscover-webcore-integration.md']:
 sources[path]=sha(ROOT/path)
shutil.copy2(qemu,OUT/'qemu-final.log')
reports=OUT/'reports';reports.mkdir(exist_ok=True)
for path in ['build/webcore-headless-page/report.json','build/webcore-headless-page/compile-report.json','build/webcore-engine-openssl/report.json','build/webcore-engine-openssl/compile-report.json','build/user-runtime/base-programs.json','build/sacx/Dihscover-engine.json']:
 target=reports/Path(path).parent.name/Path(path).name;target.parent.mkdir(exist_ok=True);shutil.copy2(ROOT/path,target)
shutil.copytree(ROOT/'build/native-controls-validation/licenses',OUT/'licenses',dirs_exist_ok=True)
with zipfile.ZipFile(OUT/'KERNEL-SOURCES.zip','w',zipfile.ZIP_DEFLATED) as archive:
 for path in snapshot['files']:archive.write(OUT/'source'/path,path)
 archive.writestr('SOURCE-MANIFEST.json',json.dumps(snapshot,indent=2)+'\n')
evidence={}
for directory in [reports,OUT/'licenses',OUT/'host',OUT/'window-host']:
 for path in directory.rglob('*'):
  if path.is_file() and (directory==OUT/'licenses' or path.suffix in ['.json','.log','.txt','.TXT','.LICENSE','.md']):evidence[path.relative_to(ROOT).as_posix()]=sha(path)
for name in ['qemu-final.log','qemu-run.log','host-tests.log','window-tests.log','frontend-build.log','kernel-build.log','source.json','KERNEL-SOURCES.zip']:
 evidence[(OUT/name).relative_to(ROOT).as_posix()]=sha(OUT/name)
proof={'date':'2026-11-02','scope':'Existing Dihscover frontend with an isolated embedded WebCore renderer',
 'existing_frontend':'sdk/sacx/apps/dihscover/main.cpp','normal_application':'OS/System/Programs/Dihscover/Dihscover.sacx',
 'renderer':'OS/System/Programs/Dihscover/WebCore.bin','separate_preview_toolbar_linked':False,'manual_helper_launch_required':False,
 'qemu_validated':True,'qemu_groups':sum(line.startswith('PASS:') for line in log.splitlines()),'qemu_network_modeled':True,
 'host_existing_ui_validated':True,'desktop_adapter_built':True,'hyperv_guest_validated':False,'physical_hardware_validated':False,
 'frontend_sha256':metadata['frontend_sha256'],'renderer_sha256':page['elf_sha256'],'kernel_sha256':sha(kernel),
 'tls_verification_inherited_from':'docs/browser-evaluation/native-controls-20261102.json','unchanged_transport_sources':tls_sources,
 'source_hashes':sources,'proof_files':evidence,
 'tests':['original window/buttons/search field','Go/search and Back callbacks','saved font/zoom settings','foreground keyboard routing','mouse deduplication',
 'API layout compatibility','session/renderer ownership and protected copies','ordered commands and overflow recovery','pending-load and close cleanup',
 'real ARM64 WebCore CSS/JavaScript pixels','embedded surface resize','Stop/Reload','input editing and preventDefault','Back/Forward history','all native process pages reclaimed'],
 'limits':['one WebCore browser session','1024x768 maximum page surface','64 KiB response bound','bitmap decoding, cookies and media incomplete',
 'trusted existing SACX GUI frontend; content renderer in EL0','live Hyper-V application smoke test pending']}
DEST.write_text(json.dumps(proof,indent=2)+'\n')
print('Recorded existing Dihscover UI integration:',proof['qemu_groups'],'ARM64 groups')
