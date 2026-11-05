"""Package a frontend/broker-only fix, preserving the verified cookie renderer."""
from pathlib import Path
import hashlib,json,shutil,zipfile,datetime,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/dihscover-request-recovery-validation';SRC=ROOT/'build/rr/src'
BASE=ROOT/'build/dihscover-startup-speed-20261103'
PACKAGE=ROOT/'build/dihscover-request-recovery-20261103'
DEST=ROOT/'docs/browser-evaluation/dihscover-request-recovery-20261103.json'
PREVIOUS=ROOT/'docs/browser-evaluation/dihscover-startup-speed-20261103.json'
APP='OS/System/Programs/Dihscover/'
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def check_package():
    manifest=json.loads((PACKAGE/'manifest.json').read_text(encoding='utf-8-sig'))
    proof=json.loads(DEST.read_text());paths=set()
    for f in manifest['files']:
        p=(PACKAGE/f['path']).resolve();assert p.is_relative_to(PACKAGE) and f['path'] not in paths
        paths.add(f['path']);assert p.stat().st_size==f['size'] and sha(p).upper()==f['sha256'],f['path']
    assert paths=={p.relative_to(PACKAGE).as_posix() for p in PACKAGE.rglob('*') if p.is_file() and p.name!='manifest.json'}
    for n,d in proof['source_hashes'].items():assert sha(ROOT/n)==d,n
    for n,d in proof['proof_files'].items():assert sha(ROOT/n)==d,n
    with zipfile.ZipFile(PACKAGE/(APP+'SOURCES.zip')) as z:
        for n,d in proof['source_hashes'].items():assert hashlib.sha256(z.read(n)).hexdigest()==d,n
    with zipfile.ZipFile(PACKAGE/(APP+'KERNEL-SOURCES.zip')) as z:
        snapshot=json.loads(z.read('SOURCE-MANIFEST.json'))
        for n,d in snapshot['files'].items():assert hashlib.sha256(z.read(n)).hexdigest()==d,n
    assert sha(PACKAGE/(APP+'WebCore.bin'))==proof['renderer_sha256']
    assert sha(PACKAGE/'OS/aa64/KERNEL.ELF')==proof['kernel_sha256']
    assert sha(PACKAGE/(APP+'Dihscover.sacx'))==proof['frontend_sha256']
    assert json.loads((PACKAGE/(APP+'INTEGRATION.json')).read_text())==proof
    allowed={'EFI/BOOT/BOOTAA64.EFI','OS/aa64/STAGE2.EFI','OS/aa64/KERNEL.ELF','OS/System/BUILD.TXT'}
    allowed|={APP+n for n in ['Dihscover.sacx','WebCore.bin','ENGINE.json','INTEGRATION.json','WEBCORE.md','KERNEL-SOURCES.zip','SOURCES.zip']}
    for f in json.loads((BASE/'manifest.json').read_text(encoding='utf-8-sig'))['files']:
        if f['path'] not in allowed:assert sha(PACKAGE/f['path']).upper()==f['sha256'],f['path']
    assert sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d'
    print('PASS: package hashes, matched renderer/frontend/kernel, preserved resources, sources and inherited evidence')
if '--check' in sys.argv:
    check_package();raise SystemExit
assert not PACKAGE.exists() and not DEST.exists(),'Do not overwrite sealed packages'
previous=json.loads(PREVIOUS.read_text());sources=dict(previous['source_hashes'])
allowed={'kernel/apps/sacx_browser_session.inc','sdk/sacx/include/sacx_browser_session.h',
         'sdk/sacx/apps/dihscover/main.cpp','sdk/sacx/apps/dihscover/webcore_backend.cpp',
         'tools/browser_existing_ui_tests.cpp','tools/browser_session_lifecycle_tests.cpp',
         'kernel/process/dihos_user_loader.c','tools/user_loader_tests.c',
         'sdk/sacx/include/sacx_http_limits.h','sdk/sacx/include/sacx_user_http_request.h',
         'kernel/include/net/http_request_headers.h','kernel/include/apps/sacx_network_internal.h',
         'kernel/net/knet_usb.c','tools/sacx_network_service_tests.cpp','tools/http_request_headers_tests.c',
         'tools/webcore_dihos/headless_page_fixture.cpp','tools/webcore_dihos/platform/ResourceHandleDihOS.cpp',
         'tools/webcore_dihos/webcore_resource_fixture.cpp','tools/user_runtime_qemu_tests.c'}
for n,d in sources.items():
    if n not in allowed:assert sha(ROOT/n)==d,n
for n in allowed:sources[n]=sha(ROOT/n)
for n in ['tools/webcore_dihos/package_request_recovery.py','tools/webcore_dihos/prepare_request_snapshot.py','tools/webcore_dihos/build_request_limits.py','tools/webcore_dihos/resume_request_build.py','tools/webcore_dihos/test_request_recovery.py','docs/dihscover-request-recovery.md']:
    sources[n]=sha(ROOT/n)
for n,d in previous['proof_files'].items():assert sha(ROOT/n)==d,n
meta=json.loads((OUT/'frontend/Dihscover-engine.json').read_text(encoding='utf-8-sig'))
renderer=json.loads((OUT/'renderer/report.json').read_text())
assert renderer['linked'] and renderer['loader_accepted'] and renderer['upstream_archives_unchanged']
assert meta['engine_sha256']==renderer['elf_sha256']==sha(OUT/'frontend/Dihscover-WebCore.bin')
assert meta['frontend_sha256']==sha(OUT/'frontend/Dihscover.sacx')
for n,d in meta['sources'].items():assert sha(ROOT/n)==d,n;sources[n]=d
snapshot=json.loads((ROOT/'build/rr/source.json').read_text())
def source_file(n):return Path(snapshot.get('source_overrides',{}).get(n,str(SRC/n)))
for n,d in snapshot['files'].items():assert sha(source_file(n))==d,n
assert snapshot['files']['kernel/include/net/dihos_tls_extra_ca.h']==previous['extra_ca']['header_sha256']
required={'ui-tests.log':['PASS: startup progress, immediate startup cancellation','PASS: executable-read progress preserves pending ownership','PASS: Dihscover existing frontend'],
          'kernel-build.log':['Built:','Copy to USB:'],
          'frontend-build.log':['Built SACX app:']}
required['ui-tests.log']+=['PASS: status text, progress bar and viewport stay separated','PASS: modeled 100 MiB browser read uses 1602 polls versus 25602 ordinary','PASS: stopped/faulted renderer cleanup']
required['http-tests.log']=['PASS: 16 KiB request headers and 16 KiB POST bodies are owned']
required['header-tests.log']=['PASS: bounded request fields']
required['qemu-page/run.log']=['WebCore 16 KiB requests:','PASS: embedded WebCore uses the existing-UI command bridge','PASS: iframe DOM and HTTP share session cookies','PASS: inline SVG, data SVG, external SVG and PNG/JPEG/GIF/BMP/WebP paint real pixels']
for n,markers in required.items():
    text=(OUT/n).read_text()
    for marker in markers:assert marker in text,(n,marker)
    assert 'FAIL:' not in text and 'Assertion failed:' not in text,n
with zipfile.ZipFile(OUT/'KERNEL-SOURCES.zip','w',zipfile.ZIP_DEFLATED) as z:
    for n in snapshot['files']:z.write(source_file(n),n)
    z.writestr('SOURCE-MANIFEST.json',json.dumps(snapshot,indent=2)+'\n')
evidence=dict(previous['proof_files']);evidence[PREVIOUS.relative_to(ROOT).as_posix()]=sha(PREVIOUS)
for n in [*required,'KERNEL-SOURCES.zip','renderer/report.json','qemu-page/report.json']:
    evidence[(OUT/n).relative_to(ROOT).as_posix()]=sha(OUT/n)
evidence['build/rr/source.json']=sha(ROOT/'build/rr/source.json')
qemu=json.loads((OUT/'qemu-page/report.json').read_text())
assert qemu['passed'] and qemu['image_test_sha256']==renderer['elf_sha256'] and qemu['fixture_sha256']==renderer['fixture_elf_sha256']
assert qemu['inputs']['tools/user_runtime_qemu_tests.c']==sha(ROOT/'tools/user_runtime_qemu_tests.c')
assert 'transform:translate3d(0,0,0);will-change:transform' in (ROOT/'tools/user_runtime_qemu_tests.c').read_text()
for n,d in renderer['sources'].items():
    normalized=n.replace('\\','/');marker=ROOT.name+'/'
    if marker in normalized:
        name=normalized.split(marker,1)[1];assert sha(ROOT/name)==d,name;sources[name]=d
proof={**previous,'scope':'16 KiB requests/engine reads, software compositing and dead-renderer recovery',
       'frontend_sha256':meta['frontend_sha256'],'kernel_sha256':sha(SRC/'OS/aa64/KERNEL.ELF'),
       'renderer_sha256':renderer['elf_sha256'],'renderer_rebuilt':True,
       'source_hashes':sources,'proof_files':evidence,'browser_frontend_operations_appended':[8],
       'startup_progress_guest_validated':False,'hyperv_guest_validated':False}
proof['loader_browser_budget_ms']=4
proof['loader_read_max_bytes']=16384
proof['request_header_max_bytes']=16384;proof['request_body_max_bytes']=16384
proof['accelerated_compositing_enabled']=False
proof['qemu_groups']=qemu['groups']
proof['limits']=[s for s in proof['limits'] if 'executable read throughput unchanged' not in s and '2048-byte request' not in s]
proof['limits']+=['16 KiB request headers and memory-only POST bodies; no file/blob uploads or streaming request uploads']
proof['limits']+=['Browser startup speed and UI latency need guest validation; modeled read test is not a VM timing result']
manifest=json.loads((BASE/'manifest.json').read_text(encoding='utf-8-sig'))
for f in manifest['files']:assert sha(BASE/f['path']).upper()==f['sha256'],f['path']
DEST.write_text(json.dumps(proof,indent=2)+'\n');shutil.copytree(BASE,PACKAGE)
for target,source in {'EFI/BOOT/BOOTAA64.EFI':SRC/'build/BOOTAA64.EFI','OS/aa64/STAGE2.EFI':SRC/'build/STAGE2.EFI',
    'OS/aa64/KERNEL.ELF':SRC/'OS/aa64/KERNEL.ELF',APP+'Dihscover.sacx':OUT/'frontend/Dihscover.sacx',
    APP+'WebCore.bin':OUT/'frontend/Dihscover-WebCore.bin',
    APP+'ENGINE.json':OUT/'frontend/Dihscover-engine.json',APP+'KERNEL-SOURCES.zip':OUT/'KERNEL-SOURCES.zip',
    APP+'INTEGRATION.json':DEST,APP+'WEBCORE.md':ROOT/'docs/dihscover-request-recovery.md'}.items():shutil.copy2(source,PACKAGE/target)
with zipfile.ZipFile(PACKAGE/(APP+'SOURCES.zip'),'w',zipfile.ZIP_DEFLATED) as z:
    for n in sorted(sources):z.write(ROOT/n,n)
    z.write(DEST,DEST.relative_to(ROOT).as_posix())
build=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+proof['kernel_sha256'][:12].upper()
(PACKAGE/'OS/System/BUILD.TXT').write_text('DihOS ARM64 Hyper-V build '+build+'\nSystem file hashes: 0:/manifest.json\n',encoding='ascii')
files=[{'path':p.relative_to(PACKAGE).as_posix(),'size':p.stat().st_size,'sha256':sha(p).upper()} for p in sorted(PACKAGE.rglob('*')) if p.is_file() and p.name!='manifest.json']
(PACKAGE/'manifest.json').write_text(json.dumps({'version':1,'build':build,'files':files},indent=2)+'\n',encoding='utf-8-sig')
check_package()
(OUT/'package-result.json').write_text(json.dumps({'package':str(PACKAGE),'build':build,'files':len(files),'vhd_updated':False},indent=2)+'\n')
print('Built '+build+'; '+str(len(files))+' files; VHD unchanged')
