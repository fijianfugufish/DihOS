"""Seal exact image/rendering binaries with inherited assets and test evidence."""
from pathlib import Path
import hashlib,json,shutil,zipfile,datetime
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/dihscover-cookie-validation';SRC=ROOT/'build/capt/src'
BASE=ROOT/'build/dihscover-images-perf-20261103';PACKAGE=ROOT/'build/dihscover-session-cookies-20261103'
PREVIOUS=ROOT/'docs/browser-evaluation/dihscover-images-perf-20261103.json'
DEST=ROOT/'docs/browser-evaluation/dihscover-session-cookies-20261103.json'
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
def check(ok,why):
 if not ok:raise SystemExit(why)
check(not PACKAGE.exists() and not DEST.exists(),'Preserve sealed packages; choose a new output')
previous=json.loads(PREVIOUS.read_text())
allowed={'tools/webcore_dihos/platform/WebCoreResourceLoad.cpp', 'tools/webcore_dihos/platform/WebCoreResourceLoad.h', 'tools/webcore_dihos/headless_page_fixture.cpp', 'tools/user_runtime_qemu_tests.c', 'tools/sacx_network_service_tests.cpp', 'tools/user_sacx_tests.c', 'tools/webcore_dihos/platform/ResourceHandleDihOS.cpp', 'kernel/process/dihos_user_sacx.c', 'tools/webcore_dihos/browser_embedded.cpp', 'tools/webcore_dihos/ResourceLoad.h', 'tools/webcore_dihos/headless_page_platform.cpp', 'sdk/sacx/include/sacx_user_http_request.h', 'tools/webcore_dihos/navigation_client.h', 'tools/webcore_dihos/ResourceLoad.cpp', 'tools/webcore_dihos/webcore_resource_fixture.cpp', 'kernel/net/knet_usb.c', 'kernel/apps/sacx_user_network.inc'}
for n,d in previous['source_hashes'].items():
 if n not in allowed:check(sha(ROOT/n)==d,'Inherited source changed: '+n)
for n,d in previous['proof_files'].items():check(sha(ROOT/n)==d,'Inherited evidence changed: '+n)
check(sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d','Working kernel changed')
snapshot=json.loads((OUT/'source-short.json').read_text())
for n,d in snapshot['files'].items():check(sha(SRC/n)==d,'Snapshot changed: '+n)
check(snapshot['files']['kernel/include/net/dihos_tls_extra_ca.h']==previous['extra_ca']['header_sha256'],'Personal CA changed')
renderer=json.loads((OUT/'renderer/report.json').read_text())
check(renderer['linked'] and renderer['loader_accepted'] and renderer['upstream_archives_unchanged'],'Renderer incomplete')
check(sha(OUT/'renderer/page.elf')==renderer['elf_sha256'],'Renderer changed')
qemu=json.loads((OUT/'qemu-page/report.json').read_text())
check(qemu['passed'] and qemu['fixture_sha256']==renderer['fixture_elf_sha256'],'QEMU resource mismatch')
check(qemu['image_test_sha256']==renderer['elf_sha256'],'QEMU image decoder mismatch')
required={'kernel-build.log':['Built:','Copy to USB:'],'frontend-build.log':['Built SACX app:'],
 'ui-tests.log':['PASS: Dihscover existing frontend'],'host-cookies.log':['PASS: official PSL','PASS: 10000 deterministic'],
 'hyperv-tests.log':['PASS: deployment script syntax'],'sanitizers.log':['PASS: official PSL','PASS: 10000 deterministic'],'http-tests.log':['PASS: appended GET/POST descriptor'],
 'qemu-page/run.log':['PASS: inline SVG, data SVG, external SVG and PNG/JPEG/GIF/BMP/WebP paint real pixels','PASS: twenty plain pointer movements cause zero additional viewport uploads','PASS: embedded WebCore uses the existing-UI command bridge','PASS: sparse quota/slot bounds','PASS: iframe DOM and HTTP share session cookies','WebCore cookies: credential omission']}
for n,markers in required.items():
 text=(OUT/n).read_text()
 for marker in markers:check(marker in text,'Missing check: '+n+': '+marker)
 check('FAIL:' not in text and 'Assertion failed:' not in text,'Failed check: '+n)
frontend=OUT/'frontend';meta=json.loads((frontend/'Dihscover-engine.json').read_text(encoding='utf-8-sig'))
check(meta['engine_sha256']==renderer['elf_sha256'] and sha(frontend/'Dihscover-WebCore.bin')==renderer['elf_sha256'],'Frontend/renderer mismatch')
check(sha(frontend/'Dihscover.sacx')==meta['frontend_sha256'],'Frontend changed')
sources={n:sha(ROOT/n) for n in previous['source_hashes']}
for n,d in snapshot['files'].items():
 if (ROOT/n).is_file() and sha(ROOT/n)==d:sources[n]=d
for n,d in meta['sources'].items():check(sha(ROOT/n)==d,'Frontend source changed');sources[n]=d
for n,d in renderer['sources'].items():
 marker=ROOT.name+'/'
 if marker in n:
  relative=n.split(marker,1)[1];check(sha(ROOT/relative)==d,'Renderer source changed: '+relative);sources[relative]=d
for directory in [ROOT/'tools/webcore_dihos',ROOT/'tools/webcore_dihos/platform',ROOT/'tools/browser_fixtures/images']:
 for p in directory.iterdir():
  if p.is_file():sources[p.relative_to(ROOT).as_posix()]=sha(p)
for n in ['tools/browser_repaint_tests.cpp','tools/make_browser_image_fixtures.py','tools/prepare_browser_psl.py','tools/browser_cookie_tests.cpp','docs/dihscover-session-cookies.md','kernel/include/third_party/stb_image.h','third_party/simplewebp/simplewebp.h']:
 sources[n]=sha(ROOT/n)
frame_header=ROOT/'build/dihscover-frames-validation/renderer/headers/WebCore/EmptyFrameLoaderClient.h'
check(sha(frame_header)==renderer['frame_header_sha256'],'Frame hook changed')
sources[frame_header.relative_to(ROOT).as_posix()]=sha(frame_header)
for psl in ['public_suffix_list.dat','browser_psl.inc','browser_psl.json','PSL-MPL-2.0.txt']:
 sources[(OUT/psl).relative_to(ROOT).as_posix()]=sha(OUT/psl)
psl_meta=json.loads((OUT/'browser_psl.json').read_text())
check(psl_meta['input_sha256']==sha(OUT/'public_suffix_list.dat')==renderer['psl_sha256'],'PSL input mismatch')
check(psl_meta['output_sha256']==sha(OUT/'browser_psl.inc'),'PSL table mismatch')
check('Mozilla Public License Version 2.0' in (OUT/'PSL-MPL-2.0.txt').read_text(),'PSL licence missing')
with zipfile.ZipFile(OUT/'KERNEL-SOURCES.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n in snapshot['files']:z.write(SRC/n,n)
 z.writestr('SOURCE-MANIFEST.json',json.dumps(snapshot,indent=2)+'\n')
evidence=dict(previous['proof_files'])
for n in list(required)+['source-short.json','KERNEL-SOURCES.zip','renderer/report.json','qemu-page/report.json']:
 evidence[(OUT/n).relative_to(ROOT).as_posix()]=sha(OUT/n)
manifest=json.loads((BASE/'manifest.json').read_text(encoding='utf-8-sig'))
for item in manifest['files']:
 p=(BASE/item['path']).resolve();check(p.is_relative_to(BASE) and p.stat().st_size==item['size'] and sha(p).upper()==item['sha256'],'Baseline package mismatch')
kernel=SRC/'OS/aa64/KERNEL.ELF'
proof={'date':'2026-11-03','scope':'Native session cookies, credential/origin restrictions, iframes and memory-only POST for verification compatibility',
 'frontend_sha256':meta['frontend_sha256'],'renderer_sha256':renderer['elf_sha256'],'kernel_sha256':sha(kernel),
 'public_api_layout_changed':False,'school_ca_changed':False,'sacx_operations_appended':[40],'extra_ca':previous['extra_ca'],
 'arm64_page_tests_passed':True,'qemu_network_modeled':True,'qemu_groups':qemu['groups'],
 'upstream_engine_archives_unchanged':True,'renderer_cpu_budget_us':4000,'session_cookies':True,'cookie_store_api_exposed':False,'google_captcha_validated':False,'psl':psl_meta,
 'source_hashes':sources,'proof_files':evidence,'hyperv_guest_validated':False,'physical_hardware_validated':False,
 'limits':['Guest FPS/latency retest pending','Session-only cookie storage, 128 cookies and no Partitioned Cookie Store API','No AVIF or image colour-profile support','Existing media limitations and school filtering remain','Real Google challenge and guest retest pending','2048-byte request headers and 4096-byte memory-only POST bodies']}
DEST.write_text(json.dumps(proof,indent=2)+'\n')
shutil.copytree(BASE,PACKAGE)
app_path='OS/System/Programs/Dihscover';app=PACKAGE/app_path
changed={'EFI/BOOT/BOOTAA64.EFI':SRC/'build/BOOTAA64.EFI','OS/aa64/STAGE2.EFI':SRC/'build/STAGE2.EFI','OS/aa64/KERNEL.ELF':kernel,
 app_path+'/Dihscover.sacx':frontend/'Dihscover.sacx',app_path+'/WebCore.bin':frontend/'Dihscover-WebCore.bin',app_path+'/ENGINE.json':frontend/'Dihscover-engine.json'}
for n,p in changed.items():shutil.copy2(p,PACKAGE/n)
shutil.copy2(DEST,app/'INTEGRATION.json');shutil.copy2(ROOT/'docs/dihscover-session-cookies.md',app/'WEBCORE.md')
shutil.copy2(OUT/'KERNEL-SOURCES.zip',app/'KERNEL-SOURCES.zip')
shutil.copy2(BASE/app_path/'SOURCES.zip',app/'RENDERER-BASELINE-SOURCES.zip')
with zipfile.ZipFile(app/'SOURCES.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n in sorted(sources):z.write(ROOT/n,n)
 z.write(DEST,DEST.relative_to(ROOT).as_posix())
licences=app/'LICENCES';licences.mkdir(exist_ok=True)
stb=(ROOT/'kernel/include/third_party/stb_image.h').read_text()
(licences/'stb_image.txt').write_text(stb[stb.index('This software is available under 2 licenses'):])
webp=(ROOT/'third_party/simplewebp/simplewebp.h').read_text()
start=webp.rfind('/*',0,webp.index('Additional IP Rights Grant'))
(licences/'simplewebp.txt').write_text(webp[start:])
shutil.copy2(OUT/'PSL-MPL-2.0.txt',licences/'PSL-MPL-2.0.txt')
shutil.copy2(OUT/'public_suffix_list.dat',licences/'public_suffix_list.dat')
build=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+proof['kernel_sha256'][:12].upper()
(PACKAGE/'OS/System/BUILD.TXT').write_text('DihOS ARM64 Hyper-V build '+build+'\nSystem file hashes: 0:/manifest.json\n',encoding='ascii')
mutable=set(changed)|{'OS/System/BUILD.TXT'}|{app_path+'/'+n for n in ['INTEGRATION.json','WEBCORE.md','KERNEL-SOURCES.zip','SOURCES.zip','RENDERER-BASELINE-SOURCES.zip']}
for item in manifest['files']:
 if item['path'] not in mutable:check(sha(PACKAGE/item['path']).upper()==item['sha256'],'Baseline resource changed: '+item['path'])
files=[{'path':p.relative_to(PACKAGE).as_posix(),'size':p.stat().st_size,'sha256':sha(p).upper()} for p in sorted(PACKAGE.rglob('*')) if p.is_file() and p!=PACKAGE/'manifest.json']
(PACKAGE/'manifest.json').write_text(json.dumps({'version':1,'build':build,'files':files},indent=2)+'\n',encoding='utf-8-sig')
for item in files:check(sha(PACKAGE/item['path']).upper()==item['sha256'],'Packaged hash mismatch')
result={'package':str(PACKAGE),'build':build,'files':len(files),'vhd_updated':False,'hyperv_guest_validated':False,'google_captcha_validated':False,'session_cookies':True}
(OUT/'package-result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
