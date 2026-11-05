"""Package a frontend/broker-only fix, preserving the verified cookie renderer."""
from pathlib import Path
import hashlib,json,shutil,zipfile,datetime,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/dihscover-startup-speed-validation';SRC=ROOT/'build/stp/src'
BASE=ROOT/'build/dihscover-startup-progress-20261103'
PACKAGE=ROOT/'build/dihscover-startup-speed-20261103'
DEST=ROOT/'docs/browser-evaluation/dihscover-startup-speed-20261103.json'
PREVIOUS=ROOT/'docs/browser-evaluation/dihscover-startup-progress-20261103.json'
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
    assert sha(PACKAGE/(APP+'WebCore.bin'))==sha(BASE/(APP+'WebCore.bin'))==proof['renderer_sha256']
    assert sha(PACKAGE/'OS/aa64/KERNEL.ELF')==proof['kernel_sha256']
    assert sha(PACKAGE/(APP+'Dihscover.sacx'))==proof['frontend_sha256']
    assert json.loads((PACKAGE/(APP+'INTEGRATION.json')).read_text())==proof
    allowed={'EFI/BOOT/BOOTAA64.EFI','OS/aa64/STAGE2.EFI','OS/aa64/KERNEL.ELF','OS/System/BUILD.TXT'}
    allowed|={APP+n for n in ['Dihscover.sacx','ENGINE.json','INTEGRATION.json','WEBCORE.md','KERNEL-SOURCES.zip','SOURCES.zip']}
    for f in json.loads((BASE/'manifest.json').read_text(encoding='utf-8-sig'))['files']:
        if f['path'] not in allowed:assert sha(PACKAGE/f['path']).upper()==f['sha256'],f['path']
    assert sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d'
    print('PASS: package hashes, exact unchanged cookie renderer/resources, frontend, isolated kernel, sources and inherited evidence')
if '--check' in sys.argv:
    check_package();raise SystemExit
assert not PACKAGE.exists() and not DEST.exists(),'Do not overwrite sealed packages'
previous=json.loads(PREVIOUS.read_text());sources=dict(previous['source_hashes'])
allowed={'kernel/apps/sacx_browser_session.inc','sdk/sacx/include/sacx_browser_session.h',
         'sdk/sacx/apps/dihscover/main.cpp','sdk/sacx/apps/dihscover/webcore_backend.cpp',
         'tools/browser_existing_ui_tests.cpp','tools/browser_session_lifecycle_tests.cpp',
         'kernel/process/dihos_user_loader.c','tools/user_loader_tests.c'}
for n,d in sources.items():
    if n not in allowed:assert sha(ROOT/n)==d,n
for n in allowed:sources[n]=sha(ROOT/n)
for n in ['tools/webcore_dihos/package_startup_speed.py','tools/webcore_dihos/build_startup_budget.py','docs/dihscover-startup-speed.md']:
    sources[n]=sha(ROOT/n)
for n,d in previous['proof_files'].items():assert sha(ROOT/n)==d,n
meta=json.loads((OUT/'frontend/Dihscover-engine.json').read_text(encoding='utf-8-sig'))
assert meta['engine_sha256']==previous['renderer_sha256']==sha(OUT/'frontend/Dihscover-WebCore.bin')
assert meta['frontend_sha256']==sha(OUT/'frontend/Dihscover.sacx')
for n,d in meta['sources'].items():assert sha(ROOT/n)==d,n;sources[n]=d
snapshot=json.loads((OUT/'source.json').read_text())
def source_file(n):return Path(snapshot.get('source_overrides',{}).get(n,str(SRC/n)))
for n,d in snapshot['files'].items():assert sha(source_file(n))==d,n
assert snapshot['files']['kernel/include/net/dihos_tls_extra_ca.h']==previous['extra_ca']['header_sha256']
required={'ui-tests.log':['PASS: startup progress, immediate startup cancellation','PASS: executable-read progress preserves pending ownership','PASS: Dihscover existing frontend'],
          'kernel-build.log':['PASS: baseline kernel reproduced byte-for-byte'],
          'frontend-build.log':['Built SACX app:']}
required['ui-tests.log']+=['PASS: status text, progress bar and viewport stay separated','PASS: modeled 100 MiB browser read uses 6402 polls versus 25602 ordinary']
for n,markers in required.items():
    text=(OUT/n).read_text()
    for marker in markers:assert marker in text,(n,marker)
    assert 'FAIL:' not in text and 'Assertion failed:' not in text,n
with zipfile.ZipFile(OUT/'KERNEL-SOURCES.zip','w',zipfile.ZIP_DEFLATED) as z:
    for n in snapshot['files']:z.write(source_file(n),n)
    z.writestr('SOURCE-MANIFEST.json',json.dumps(snapshot,indent=2)+'\n')
evidence=dict(previous['proof_files']);evidence[PREVIOUS.relative_to(ROOT).as_posix()]=sha(PREVIOUS)
for n in [*required,'KERNEL-SOURCES.zip','source.json','kernel-report.json','loader-flags.json','compile-loader.log','baseline-relink.log','link-kernel.log']:
    evidence[(OUT/n).relative_to(ROOT).as_posix()]=sha(OUT/n)
kernel_report=json.loads((OUT/'kernel-report.json').read_text())
assert kernel_report['baseline_reproduced'] and kernel_report['original_objects_unchanged']
assert kernel_report['kernel_sha256']==sha(OUT/'KERNEL.ELF')
assert kernel_report['loader_source_sha256']==sha(ROOT/'kernel/process/dihos_user_loader.c')
proof={**previous,'scope':'Measured status spacing and bounded four-ms browser executable read budget; cookie renderer unchanged',
       'frontend_sha256':meta['frontend_sha256'],'kernel_sha256':sha(OUT/'KERNEL.ELF'),
       'source_hashes':sources,'proof_files':evidence,'browser_frontend_operations_appended':[8],
       'renderer_rebuilt':False,'startup_progress_guest_validated':False,'hyperv_guest_validated':False}
proof['loader_browser_budget_ms']=4
proof['loader_read_max_bytes']=4096
proof['limits']=[s for s in proof['limits'] if 'executable read throughput unchanged' not in s]
proof['limits']+=['Browser startup speed and UI latency need guest validation; modeled read test is not a VM timing result']
manifest=json.loads((BASE/'manifest.json').read_text(encoding='utf-8-sig'))
for f in manifest['files']:assert sha(BASE/f['path']).upper()==f['sha256'],f['path']
DEST.write_text(json.dumps(proof,indent=2)+'\n');shutil.copytree(BASE,PACKAGE)
for target,source in {'EFI/BOOT/BOOTAA64.EFI':SRC/'build/BOOTAA64.EFI','OS/aa64/STAGE2.EFI':SRC/'build/STAGE2.EFI',
    'OS/aa64/KERNEL.ELF':OUT/'KERNEL.ELF',APP+'Dihscover.sacx':OUT/'frontend/Dihscover.sacx',
    APP+'ENGINE.json':OUT/'frontend/Dihscover-engine.json',APP+'KERNEL-SOURCES.zip':OUT/'KERNEL-SOURCES.zip',
    APP+'INTEGRATION.json':DEST,APP+'WEBCORE.md':ROOT/'docs/dihscover-startup-speed.md'}.items():shutil.copy2(source,PACKAGE/target)
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
