"""Package bounded URLs, response-header fixes and the Google startup address."""
from pathlib import Path
import hashlib,json,shutil,zipfile,datetime
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/dihscover-loading-fix-validation';SRC=ROOT/'build/loadfix/src'
BASE=ROOT/'build/dihscover-google-school-ca-20261103'
PACKAGE=ROOT/'build/dihscover-loading-fix-20261103'
PREVIOUS=ROOT/'docs/browser-evaluation/dihscover-school-ca-20261103.json'
DEST=ROOT/'docs/browser-evaluation/dihscover-loading-fix-20261103.json'
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def check(ok,why):
    if not ok:raise SystemExit(why)
check(not DEST.exists() and not PACKAGE.exists(),'Retain existing proof/package; use a fresh milestone')
previous=json.loads(PREVIOUS.read_text())
allowed={'tools/webcore_dihos/ResourceLoad.cpp','tools/webcore_dihos/webcore_resource_fixture.cpp','tools/webcore_dihos/platform/ResourceHandleDihOS.cpp','tools/webcore_dihos/platform/WebCoreResourceLoad.cpp','tools/webcore_dihos/platform/WebCoreResourceLoad.h','sdk/sacx/apps/dihscover/build_webcore.ps1','sdk/sacx/apps/dihscover/main.cpp','sdk/sacx/include/sacx_user_network.h','kernel/apps/sacx_runtime.cpp','kernel/include/apps/sacx_network_internal.h','kernel/net/knet_usb.c','tools/export_user_snapshot.py','tools/browser_existing_ui_tests.cpp','tools/sacx_network_service_tests.cpp','tools/probe_kernel_tls.py','tools/kernel_tls_socket_probe.c','tools/hyperv_modern_tls_tests.c'}
for name,digest in previous['source_hashes'].items():
    if name not in allowed:check(sha(ROOT/name)==digest,'Inherited source changed: '+name)
for name,digest in previous['proof_files'].items():check(sha(ROOT/name)==digest,'Inherited evidence changed: '+name)
check(sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d','Working kernel changed')
snapshot=json.loads((OUT/'source-short.json').read_text())
for name,digest in snapshot['files'].items():check(sha(SRC/name)==digest,'Snapshot changed: '+name)
check(snapshot['files']['kernel/include/net/dihos_tls_extra_ca.h']==previous['extra_ca']['header_sha256'],'Personal CA changed')
with zipfile.ZipFile(BASE/'OS/System/Programs/Dihscover/SOURCES.zip') as z:
    old=z.read('kernel/apps/sacx_runtime.cpp').decode().replace('\r\n','\n')
    for name in ['current_url','location','next_url']:
        before='char '+name+'[768];';check(old.count(before)==1,'Unexpected runtime baseline');old=old.replace(before,'char '+name+'[SACX_HTTP_URL_MAX+1];')
    check(old==(ROOT/'kernel/apps/sacx_runtime.cpp').read_text(),'Unexpected mixed runtime edits')
renderer=json.loads((OUT/'renderer/report.json').read_text());check(renderer['linked'] and renderer['loader_accepted'] and renderer['upstream_archives_unchanged'],'Renderer build incomplete')
qemu=json.loads((OUT/'qemu-page/report.json').read_text());check(qemu['passed'] and qemu['fixture_sha256']==renderer['fixture_elf_sha256'],'ARM64 resource test mismatch')
check('PASS: embedded WebCore uses the existing-UI command bridge' in (OUT/'qemu-page/run.log').read_text(),'Page execution evidence missing')
required={'kernel-build-final.log':['Built:','Copy to USB:'],'http-tests-8191-final.log':['PASS: 8191-byte URLs','PASS: ARM64 transport compiles'],
 'modern-transport-final.log':['PASS: full 8191-byte request URL'],'response-headers.log':['PASS: empty/whitespace-only response values'],
 'hyperv-tests.log':['PASS: deployment script syntax'],'ui-tests.log':['PASS: Dihscover existing frontend'],
 'frontend-build.log':['Built SACX app:'],'live-redirects.log':['PASS: Google search and Wikipedia redirect chains'],
 'qemu-page/run.log':['WebCore response fields: empty values and Latin-1 octets OK','PASS: actual WebCore request/response/error metadata','PASS: sparse quota/slot bounds']}
for name,markers in required.items():
    text=(OUT/name).read_text()
    for marker in markers:check(marker in text,'Missing check: '+name+': '+marker)
    check('FAIL:' not in text and 'Assertion failed:' not in text,'Failed check: '+name)
frontend=OUT/'frontend';meta=json.loads((frontend/'Dihscover-engine.json').read_text(encoding='utf-8-sig'))
check(meta['engine_sha256']==renderer['elf_sha256'] and sha(frontend/'Dihscover-WebCore.bin')==renderer['elf_sha256'],'Frontend/renderer mismatch')
check(sha(frontend/'Dihscover.sacx')==meta['frontend_sha256'],'Frontend hash mismatch')
sources={name:sha(ROOT/name) for name in previous['source_hashes']}
for name,digest in snapshot['files'].items():
    if (ROOT/name).is_file() and sha(ROOT/name)==digest:sources[name]=digest
for name,digest in meta['sources'].items():check(sha(ROOT/name)==digest,'Frontend source changed');sources[name]=digest
for name,digest in renderer['sources'].items():
    marker=ROOT.name+'/'
    if marker in name:
        relative=name.split(marker,1)[1];check(sha(ROOT/relative)==digest,'Renderer source changed: '+relative);sources[relative]=digest
for directory in [ROOT/'tools/webcore_dihos',ROOT/'tools/webcore_dihos/platform']:
    for p in directory.glob('*.h'):sources[p.relative_to(ROOT).as_posix()]=sha(p)
extra=['sdk/sacx/include/sacx_http_limits.h','kernel/include/net/http_limits.h','kernel/include/net/http_stream.h','kernel/net/http_stream.c','tools/response_headers_tests.cpp','tools/probe_browser_redirects.py','tools/webcore_dihos/build_loading_fix.py','tools/webcore_dihos/test_loading_fix.py','tools/webcore_dihos/package_loading_fix.py','docs/dihscover-loading-fixes.md']
for name in extra:sources[name]=sha(ROOT/name)
with zipfile.ZipFile(OUT/'KERNEL-SOURCES.zip','w',zipfile.ZIP_DEFLATED) as z:
    for name in snapshot['files']:z.write(SRC/name,name)
    z.writestr('SOURCE-MANIFEST.json',json.dumps(snapshot,indent=2)+'\n')
evidence=dict(previous['proof_files'])
for name in list(required)+['source-short.json','KERNEL-SOURCES.zip','live-redirects.json','renderer/report.json','qemu-page/report.json']:
    evidence[(OUT/name).relative_to(ROOT).as_posix()]=sha(OUT/name)
manifest=json.loads((BASE/'manifest.json').read_text(encoding='utf-8-sig'))
for item in manifest['files']:
    p=(BASE/item['path']).resolve();check(p.is_relative_to(BASE) and p.stat().st_size==item['size'] and sha(p).upper()==item['sha256'],'Baseline package mismatch')
kernel=SRC/'OS/aa64/KERNEL.ELF'
proof={'date':'2026-11-03','scope':'Complete bounded redirect URLs, valid empty/Latin-1 response fields and Google startup address',
 'frontend_sha256':meta['frontend_sha256'],'renderer_sha256':renderer['elf_sha256'],'kernel_sha256':sha(kernel),
 'url_max_bytes':8191,'public_api_layout_changed':False,'school_ca_changed':False,'extra_ca':previous['extra_ca'],
 'arm64_el0_resource_tests_passed':True,'arm64_page_tests_passed':True,'qemu_network_modeled':True,'qemu_groups':qemu['groups'],
 'host_live_redirects':json.loads((OUT/'live-redirects.json').read_text()),'upstream_engine_archives_unchanged':True,
 'source_hashes':sources,'proof_files':evidence,'hyperv_guest_validated':False,'physical_hardware_validated':False,
 'limits':['Live guest retest pending','Existing cookie/media limitations remain','School filtering and certificate validation are unchanged']}
DEST.write_text(json.dumps(proof,indent=2)+'\n')
shutil.copytree(BASE,PACKAGE)
app_path='OS/System/Programs/Dihscover';app=PACKAGE/app_path
changed={'EFI/BOOT/BOOTAA64.EFI':SRC/'build/BOOTAA64.EFI','OS/aa64/STAGE2.EFI':SRC/'build/STAGE2.EFI','OS/aa64/KERNEL.ELF':kernel,
 app_path+'/Dihscover.sacx':frontend/'Dihscover.sacx',app_path+'/WebCore.bin':frontend/'Dihscover-WebCore.bin',app_path+'/ENGINE.json':frontend/'Dihscover-engine.json'}
for name,src in changed.items():shutil.copy2(src,PACKAGE/name)
shutil.copy2(DEST,app/'INTEGRATION.json');shutil.copy2(ROOT/'docs/dihscover-loading-fixes.md',app/'WEBCORE.md')
shutil.copy2(OUT/'KERNEL-SOURCES.zip',app/'KERNEL-SOURCES.zip')
shutil.copy2(BASE/app_path/'SOURCES.zip',app/'RENDERER-BASELINE-SOURCES.zip')
with zipfile.ZipFile(app/'SOURCES.zip','w',zipfile.ZIP_DEFLATED) as z:
    for name in sorted(sources):z.write(ROOT/name,name)
    z.write(DEST,DEST.relative_to(ROOT).as_posix())
build=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+proof['kernel_sha256'][:12].upper()
(PACKAGE/'OS/System/BUILD.TXT').write_text('DihOS ARM64 Hyper-V build '+build+'\nSystem file hashes: 0:/manifest.json\n',encoding='ascii')
mutable=set(changed)|{'OS/System/BUILD.TXT'}|{app_path+'/'+n for n in ['INTEGRATION.json','WEBCORE.md','KERNEL-SOURCES.zip','SOURCES.zip','RENDERER-BASELINE-SOURCES.zip']}
for item in manifest['files']:
    if item['path'] not in mutable:check(sha(PACKAGE/item['path']).upper()==item['sha256'],'Baseline resource changed: '+item['path'])
files=[{'path':p.relative_to(PACKAGE).as_posix(),'size':p.stat().st_size,'sha256':sha(p).upper()} for p in sorted(PACKAGE.rglob('*')) if p.is_file() and p!=PACKAGE/'manifest.json']
(PACKAGE/'manifest.json').write_text(json.dumps({'version':1,'build':build,'files':files},indent=2)+'\n',encoding='utf-8-sig')
for item in files:check(sha(PACKAGE/item['path']).upper()==item['sha256'],'Packaged hash mismatch')
result={'package':str(PACKAGE),'build':build,'files':len(files),'vhd_updated':False,'hyperv_guest_validated':False,'url_max_bytes':8191,'homepage':'https://www.google.com/'}
(OUT/'package-result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
