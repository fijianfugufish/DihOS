"""Seal the tested native TLS upgrade into the normal browser package.

Never mounts a disk, launches a VM, or modifies an earlier package/proof.
"""
from pathlib import Path
import hashlib,json,shutil,zipfile,datetime
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/dihscover-modern-tls-release'
PACKAGE=ROOT/'build/dihscover-modern-tls-20261102'
BASE=ROOT/'build/dihscover-transport-bursts-20261102'
PREVIOUS=ROOT/'docs/browser-evaluation/dihscover-transport-bursts-20261102.json'
DEST=ROOT/'docs/browser-evaluation/dihscover-modern-tls-20261102.json'
HOST=ROOT/'build/kernel-tls-host-release'
NATIVE=ROOT/'build/kernel-tls-arm64-release'
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def check(ok,why):
    if not ok:raise SystemExit(why)
check(not PACKAGE.exists() and not DEST.exists(),'Retain existing package/proof; choose a new milestone')
previous=json.loads(PREVIOUS.read_text())
allowed={'kernel/net/knet_usb.c','tools/export_user_snapshot.py','tools/test_hyperv.ps1'}
for name,digest in previous['source_hashes'].items():
    if name not in allowed:check(sha(ROOT/name)==digest,'Inherited browser source changed: '+name)
for name,digest in previous['proof_files'].items():check(sha(ROOT/name)==digest,'Inherited evidence changed: '+name)
check(sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d','Working kernel changed')
snapshot=json.loads((OUT/'source.json').read_text())
for name,digest in snapshot['files'].items():check(sha(OUT/'source'/name)==digest,'Snapshot changed: '+name)
for directory in [HOST,NATIVE]:
    report=json.loads((directory/'report.json').read_text())
    check(report['compiled'] and len(report['objects'])==111,'TLS profile compilation incomplete')
    for name,digest in report['inputs'].items():check(sha(ROOT/name)==digest,'Tested TLS input changed: '+name)
    for name,digest in report['objects'].items():
        # Linux absolute paths are meaningful only inside WSL. The report is
        # already checked by both host runners; ARM64 objects are checked here.
        if not report['host']:check(sha(Path(name))==digest,'Native TLS object changed')
    shutil.copy2(directory/'report.json',OUT/('host-profile.json' if report['host'] else 'arm64-profile.json'))
imports=json.loads((OUT/'tls-native-imports.json').read_text())
check(set(imports)=={'dihos_entropy_fill','dihos_time_monotonic_ms','dihos_time_utc','knet_link_checkpoint',
 'memcmp','memcpy','memmove','memset','pmem_alloc_pages','pmem_free_pages','strchr','strcmp','strlen','strncmp','strncpy','strstr'},'Unexpected native TLS OS/runtime import')
for p in HOST.glob('*.log'):shutil.copy2(p,OUT/('tls-'+p.name))
shutil.copy2(HOST/'live-results.json',OUT/'tls-live-results.json')
required={'kernel-build.log':['Built:','Copy to USB:'],
 'hyperv-tests.log':['PASS: deployment script syntax','PASS: modern guest transport delivers multi-chunk authenticated response'],
 'http-tests.log':['PASS: ARM64 transport compiles'],
 'provenance.log':['PASS: pinned Mbed TLS 3.6.7'],
 'tls-validation.log':['PASS: native TLS profile validates RSA/ECDSA','100 cancelled handshakes, 16 OOM positions']}
for name,markers in required.items():
    log=(OUT/name).read_text()
    for marker in markers:check(marker in log,'Missing check: '+name+': '+marker)
    check('FAIL:' not in log and 'Assertion failed:' not in log,'Test failed: '+name)
live=json.loads((OUT/'tls-live-results.json').read_text())
for host in ['www.google.com','example.com','www.netsurf-browser.org']:
    result=next(r for r in live if r['host']==host)
    check(result['exit_code']==0 and 'authenticated=1 verify=0x0 error=0' in result['output'] and 'complete=1' in result['output'],'Incomplete authenticated response: '+host)
kernel=OUT/'source/OS/aa64/KERNEL.ELF';check(kernel.is_file(),'Build the isolated desktop kernel first')
manifest=json.loads((BASE/'manifest.json').read_text(encoding='utf-8-sig'))
for item in manifest['files']:
    p=(BASE/item['path']).resolve();check(p.is_relative_to(BASE) and p.stat().st_size==item['size'] and sha(p).upper()==item['sha256'].upper(),'Baseline manifest mismatch')
app_path='OS/System/Programs/Dihscover'
check(sha(BASE/app_path/'Dihscover.sacx')==previous['frontend_sha256'],'Frontend changed')
check(sha(BASE/app_path/'WebCore.bin')==previous['renderer_sha256'],'Renderer changed')
sources={name:sha(ROOT/name) for name in previous['source_hashes']}
for name,digest in snapshot['files'].items():
    if (ROOT/name).is_file() and sha(ROOT/name)==digest:sources[name]=digest
extra=['tools/build_kernel_tls.py','tools/check_kernel_tls_sources.py','tools/test_kernel_tls.py','tools/probe_kernel_tls.py',
 'tools/kernel_tls_validation_tests.c','tools/kernel_tls_socket_probe.c','tools/hyperv_modern_tls_tests.c',
 'tools/tls_response_probe.c','tools/mbedtls_response_probe.c','tools/webcore_dihos/package_modern_tls.py','docs/dihscover-modern-tls.md']
extra += [p.relative_to(ROOT).as_posix() for p in (ROOT/'tools/fixtures/tls').rglob('*') if p.is_file()]
for name in extra:sources[name]=sha(ROOT/name)
with zipfile.ZipFile(OUT/'KERNEL-SOURCES.zip','w',zipfile.ZIP_DEFLATED) as z:
    for name in snapshot['files']:z.write(OUT/'source'/name,name)
    z.writestr('SOURCE-MANIFEST.json',json.dumps(snapshot,indent=2)+'\n')
shutil.copytree(ROOT/'build/dihscover-transport-validation/licenses',OUT/'licenses',dirs_exist_ok=True)
license_dir=OUT/'licenses/mbedtls';license_dir.mkdir(exist_ok=True)
shutil.copy2(ROOT/'third_party/mbedtls/LICENSE',license_dir/'LICENSE')
if (ROOT/'third_party/mbedtls/LICENSES').is_dir():
    shutil.copytree(ROOT/'third_party/mbedtls/LICENSES',license_dir/'LICENSES',dirs_exist_ok=True)
evidence=dict(previous['proof_files'])
for p in OUT.iterdir():
    if p.is_file() and p.suffix in ['.log','.json','.zip']:evidence[p.relative_to(ROOT).as_posix()]=sha(p)
proof={'date':'2026-11-02','scope':'Shared freestanding TLS 1.2/1.3 client for Dihscover and terminal',
 'frontend_sha256':previous['frontend_sha256'],'renderer_sha256':previous['renderer_sha256'],'kernel_sha256':sha(kernel),
 'existing_frontend':previous['existing_frontend'],'normal_application':previous['normal_application'],'renderer':previous['renderer'],
 'qemu_validated':True,'qemu_groups':previous['qemu_groups'],'qemu_inherited_from':PREVIOUS.relative_to(ROOT).as_posix(),
 'qemu_network_modeled':True,'new_tls_tested_in_qemu':False,
 'host_existing_ui_validated':True,'manual_helper_launch_required':False,'hyperv_guest_validated':False,'physical_hardware_validated':False,
 'tls_library':'Mbed TLS 3.6.7','real_certificate_rejections_validated':True,'transport_tests_use_mock_tls':True,
 'exact_freestanding_engine_live_tests':live,'trust_roots_changed':False,'source_hashes':sources,'proof_files':evidence,
 'limits':['Live ARM64 Hyper-V retest required','School interception CA remains untrusted','Existing GET/cookie/media limitations remain','Host socket test does not validate NetVSC delivery']}
DEST.write_text(json.dumps(proof,indent=2)+'\n')
shutil.copytree(BASE,PACKAGE)
changed={'EFI/BOOT/BOOTAA64.EFI':OUT/'source/build/BOOTAA64.EFI','OS/aa64/STAGE2.EFI':OUT/'source/build/STAGE2.EFI','OS/aa64/KERNEL.ELF':kernel}
for name,source in changed.items():shutil.copy2(source,PACKAGE/name)
app=PACKAGE/app_path
shutil.copytree(OUT/'licenses',app/'Licenses/WebCore',dirs_exist_ok=True)
shutil.copy2(DEST,app/'INTEGRATION.json');shutil.copy2(ROOT/'docs/dihscover-modern-tls.md',app/'WEBCORE.md')
shutil.copy2(OUT/'KERNEL-SOURCES.zip',app/'KERNEL-SOURCES.zip')
with zipfile.ZipFile(app/'SOURCES.zip','w',zipfile.ZIP_DEFLATED) as z:
    for name in sorted(sources):z.write(ROOT/name,name)
    z.write(DEST,DEST.relative_to(ROOT).as_posix())
build=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+proof['kernel_sha256'][:12].upper()
(PACKAGE/'OS/System/BUILD.TXT').write_text('DihOS ARM64 Hyper-V build '+build+'\nSystem file hashes: 0:/manifest.json\n',encoding='ascii')
mutable=set(changed)|{'OS/System/BUILD.TXT'}|{app_path+'/'+name for name in ['INTEGRATION.json','WEBCORE.md','SOURCES.zip','KERNEL-SOURCES.zip']}
for item in manifest['files']:
    if item['path'] not in mutable:check(sha(PACKAGE/item['path']).upper()==item['sha256'].upper(),'Baseline resource changed: '+item['path'])
files=[{'path':p.relative_to(PACKAGE).as_posix(),'size':p.stat().st_size,'sha256':sha(p).upper()} for p in sorted(PACKAGE.rglob('*')) if p.is_file() and p!=PACKAGE/'manifest.json']
(PACKAGE/'manifest.json').write_text(json.dumps({'version':1,'build':build,'files':files},indent=2)+'\n',encoding='utf-8-sig')
for item in files:check(sha(PACKAGE/item['path']).upper()==item['sha256'],'Packaged hash mismatch')
result={'package':str(PACKAGE),'build':build,'files':len(files),'bytes':sum(x['size'] for x in files),'vhd_updated':False,'hyperv_guest_validated':False,'frontend_and_renderer_unchanged':True}
(OUT/'package-result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
