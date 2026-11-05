"""Package the executed large-script fix without replacing prior evidence."""
from pathlib import Path
import datetime,hashlib,json,shutil,sys,zipfile
ROOT=Path(__file__).resolve().parents[2]
budget='--budget' in sys.argv
memory=budget or '--memory' in sys.argv
progress=memory or '--progress' in sys.argv
OUT=ROOT/('build/dihscover-youtube-budget-validation' if budget else 'build/dihscover-youtube-memory-validation' if memory else 'build/dihscover-youtube-progress-validation' if progress else 'build/dihscover-youtube-bootstrap-validation');KERNEL=ROOT/('build/youtube-budget-kernel/src' if budget else 'build/youtube-memory-kernel/src' if memory else 'build/youtube-kernel/src')
SUPPORT=ROOT/'build/dihscover-youtube-bootstrap-validation'
BASE=ROOT/('build/dihscover-youtube-memory-20261104' if budget else 'build/dihscover-youtube-progress-20261104' if memory else 'build/dihscover-youtube-bootstrap-20261104' if progress else 'build/dihscover-mse-player-20261104')
PACKAGE=ROOT/('build/dihscover-youtube-budget-20261104' if budget else 'build/dihscover-youtube-memory-20261104' if memory else 'build/dihscover-youtube-progress-20261104' if progress else 'build/dihscover-youtube-bootstrap-20261104')
DEST=ROOT/('docs/browser-evaluation/'+PACKAGE.name+'.json')
documentation=ROOT/('docs/dihscover-youtube-budget.md' if budget else 'docs/dihscover-youtube-memory.md' if memory else 'docs/dihscover-youtube-progress.md' if progress else 'docs/dihscover-youtube-bootstrap.md')
APP='OS/System/Programs/Dihscover/'
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
changed_package={APP+n for n in ['WebCore.bin','Dihscover.sacx','ENGINE.json','INTEGRATION.json','WEBCORE.md','SOURCES.zip','KERNEL-SOURCES.zip']}
changed_package|={'OS/aa64/KERNEL.ELF','EFI/BOOT/BOOTAA64.EFI','OS/aa64/STAGE2.EFI','OS/System/BUILD.TXT'}
if progress and not memory:changed_package-={'OS/aa64/KERNEL.ELF','EFI/BOOT/BOOTAA64.EFI','OS/aa64/STAGE2.EFI',APP+'KERNEL-SOURCES.zip'}
def check():
    proof=read(DEST);manifest=read(PACKAGE/'manifest.json');seen=set()
    for item in manifest['files']:
        p=(PACKAGE/item['path']).resolve();assert p.is_relative_to(PACKAGE.resolve()) and item['path'] not in seen
        seen.add(item['path']);assert p.stat().st_size==item['size'] and sha(p).upper()==item['sha256'],item['path']
    assert seen=={p.relative_to(PACKAGE).as_posix() for p in PACKAGE.rglob('*') if p.is_file() and p.name!='manifest.json'}
    for name,digest in proof['proof_files'].items():assert sha(ROOT/name)==digest,name
    for archive,field in [('SOURCES.zip','source_hashes'),('KERNEL-SOURCES.zip',None)]:
        with zipfile.ZipFile(PACKAGE/(APP+archive)) as z:
            hashes=proof[field] if field else json.loads(z.read('SOURCE-MANIFEST.json'))['files']
            for name,digest in hashes.items():assert hashlib.sha256(z.read(name)).hexdigest()==digest,name
    assert read(PACKAGE/(APP+'INTEGRATION.json'))==proof
    for name,key in [('WebCore.bin','renderer_sha256'),('Dihscover.sacx','frontend_sha256')]:assert sha(PACKAGE/(APP+name))==proof[key]
    assert sha(PACKAGE/'OS/aa64/KERNEL.ELF')==proof['kernel_sha256']
    for item in read(BASE/'manifest.json')['files']:
        if item['path'] not in changed_package:assert sha(PACKAGE/item['path']).upper()==item['sha256'],item['path']
    assert sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d'
    print('PASS: matched 16 MiB response package, retained resources, source archives and execution evidence; VHD unchanged')
if '--check' in sys.argv:check();raise SystemExit
assert not PACKAGE.exists() and not DEST.exists(),'Do not overwrite sealed candidates'
previous=read(ROOT/'docs/browser-evaluation'/(BASE.name+'.json'))
for item in read(BASE/'manifest.json')['files']:assert sha(BASE/item['path']).upper()==item['sha256'],item['path']
renderer=read(OUT/'renderer/report.json');front=read(OUT/'frontend/Dihscover-engine.json')
video=read(OUT/'qemu/report.json');reg=read(OUT/'qemu-page/report.json');elf=read(OUT/'elf-tests/report.json');diag=read(SUPPORT/'diagnostics/report.json')
for result in [video,reg,elf,diag]:
    assert result['passed']
    for name,digest in result['inputs'].items():assert sha(ROOT/name)==digest,name
assert video['javascript_mse_executed'] and video['log_sha256']==sha(OUT/'qemu/run.log')
assert video['renderer_report_sha256']==sha(OUT/'renderer/report.json') and video['elf_sha256']==renderer['video_page_sha256']
assert 'WebCore response bounds: 12 MiB scripts, exact 16 MiB completion and oversized rejection OK' in (OUT/'qemu-page/run.log').read_text()
if progress:assert 'WebCore response deadlines: active 45-second transfer, stalled idle timeout and hard 120-second cap OK' in (OUT/'qemu-page/run.log').read_text()
if memory:assert 'SACX large browser allocation: 24 MiB owned, writable across 16 MiB and trimmable OK' in (OUT/'qemu/run.log').read_text()
if budget:
    assert 'PASS: trusted browser gets 512 MiB; ordinary and network/window-granted apps retain 256 MiB; cleanup complete' in (OUT/'qemu/run.log').read_text()
    assert 'PASS: trusted browser launch uses its dedicated budget; ordinary web launches retain ordinary admission' in (OUT/'host/loader.log').read_text()
assert 'PASS: native 16 MiB bounded responses' in (SUPPORT/'http-tests/network.log').read_text()
assert renderer['elf_sha256']==reg['image_test_sha256']==front['engine_sha256']==sha(OUT/'frontend/Dihscover-WebCore.bin')
assert renderer['fixture_elf_sha256']==reg['fixture_sha256'] and front['frontend_sha256']==sha(OUT/'frontend/Dihscover.sacx')
for name,digest in front['sources'].items():assert sha(ROOT/name)==digest,name
kernel_meta=ROOT/('build/youtube-budget-kernel/source.json' if budget else 'build/youtube-memory-kernel/source.json' if memory else 'build/youtube-kernel/source.json')
meta=read(kernel_meta)
for name,digest in meta['files'].items():assert sha(KERNEL/name)==digest,name
with zipfile.ZipFile(BASE/(APP+'KERNEL-SOURCES.zip')) as z:old_kernel=json.loads(z.read('SOURCE-MANIFEST.json'))
kernel_overlays={'kernel/apps/sacx_user_network.inc','sdk/sacx/include/sacx_http_limits.h'}
if memory:kernel_overlays={'kernel/include/process/dihos_user_memory.h','sdk/sacx/include/sacx_user_api.h','build.ps1'}
if budget:kernel_overlays={'kernel/process/dihos_user_image.inc','kernel/process/dihos_user_loader.c','kernel/include/process/dihos_user_runtime.h','kernel/process/dihos_user_runtime.c'}
for name,digest in old_kernel['files'].items():
    if name not in kernel_overlays:assert sha(KERNEL/name)==digest,name
scripts=read(SUPPORT/'scripts.json');largest=max(scripts,key=lambda item:item['size'])
assert 8*1024*1024<largest['size']<=16*1024*1024
assert sha(SUPPORT/largest['path'])==largest['sha256']
changed={'tools/webcore_dihos/package_youtube_bootstrap.py','tools/webcore_dihos/prepare_youtube_kernel.py',
 'tools/webcore_dihos/test_browser_diagnostics.py','tools/webcore_dihos/browser_diagnostics.h',
 'tools/browser_diagnostics_tests.cpp','tools/sacx_network_service_tests.cpp','tools/test_user_http_headers.ps1',
 'tools/webcore_dihos/ResourceLoad.cpp','tools/webcore_dihos/ResourceLoad.h','tools/webcore_dihos/resource_fixture.cpp',
 'tools/webcore_dihos/webcore_resource_fixture.cpp','tools/webcore_dihos/platform/WebCoreResourceLoad.cpp',
 'tools/webcore_dihos/platform/ResourceHandleDihOS.cpp','tools/webcore_dihos/browser_embedded.cpp',
 'tools/webcore_dihos/headless_page_fixture.cpp','docs/dihscover-youtube-bootstrap.md',*kernel_overlays}
changed.add(documentation.relative_to(ROOT).as_posix())
if memory:changed.discard('build.ps1') # The kernel-only script belongs to its isolated source archive.
if memory:changed|={'sdk/sacx/user_runtime/allocator.cpp','sdk/sacx/user_runtime/allocator_platform.cpp',
 'tools/user_allocator_host_tests.cpp','tools/test_user_allocator_host.sh','tools/user_runtime_qemu_tests.c',
 'tools/user_runtime_qemu_start.S','tools/webcore_dihos/test_large_allocator.py','tools/user_program.c',
 'tools/webcore_dihos/prepare_large_test_inputs.py','tools/webcore_dihos/css_parser_fallback.py'}
if budget:changed|={'tools/user_browser_budget_program.c','tools/user_loader_tests.c','tools/test_dihscover_integration.ps1'}
changed|={p.relative_to(ROOT).as_posix() for p in (ROOT/'tools/webcore_dihos/media').rglob('*') if p.is_file() and '__pycache__' not in p.parts}
sources=dict(previous['source_hashes'])
for name in changed:sources[name]=sha(ROOT/name)
evidence={}
for name in ['renderer/report.json','qemu/report.json','qemu/run.log','qemu-page/report.json','qemu-page/run.log',
 'elf-tests/report.json','engine/report.json','frontend/Dihscover-engine.json']:
    p=OUT/name;evidence[p.relative_to(ROOT).as_posix()]=sha(p)
for name in ['diagnostics/report.json','http-tests/network.log','http-tests/sacx.log','http-tests/headers.log','scripts.json']:
    p=SUPPORT/name;evidence[p.relative_to(ROOT).as_posix()]=sha(p)
evidence[kernel_meta.relative_to(ROOT).as_posix()]=sha(kernel_meta)
if memory:
    host=read(OUT/'allocator-host/report.json');assert host['passed']
    for name,digest in host['inputs'].items():assert sha(ROOT/name)==digest,name
    assert host['log_sha256']==sha(OUT/'allocator-host/run.log')
    fixture=read(OUT/'fixtures/report.json');assert fixture['passed']
    for name,digest in fixture['inputs'].items():assert sha(ROOT/name)==digest,name
    assert fixture['include_sha256']==sha(OUT/'fixtures/user_program.inc') and fixture['memory_elf_sha256']==sha(OUT/'fixtures/memory.elf')
    for name in ['allocator-host/report.json','allocator-host/run.log','fault.json','fixtures/report.json','fixtures/memory.elf','fixtures/user_program.inc']:
        p=OUT/name;evidence[p.relative_to(ROOT).as_posix()]=sha(p)
if budget:
    p=OUT/'host/loader.log';evidence[p.relative_to(ROOT).as_posix()]=sha(p)
proof={**previous,'scope':'Trusted browser memory budget and bounded smaller-pool fallback' if budget else 'Larger bounded owned allocator pools and matched kernel region accounting' if memory else 'Progress-aware response timeout with bounded idle and total deadlines' if progress else 'Larger native script responses and retained browser failure diagnostics',
 'source_hashes':sources,'proof_files':evidence,'renderer_sha256':renderer['elf_sha256'],
 'frontend_sha256':front['frontend_sha256'],'kernel_sha256':sha(KERNEL/'OS/aa64/KERNEL.ELF'),
 'response_limit_bytes':16777216,'response_limit_arm64_el0_executed':True,'additional_playback_repeats':0,
 'captured_startup_script':largest,'youtube_guest_validated':False,'hyperv_guest_validated':False,
 'limits':previous['limits']+['16 MiB response limit, four owned requests per process; live YouTube bootstrap/playback needs guest retesting']}
if progress:proof.update(response_idle_timeout_ms=30000,response_total_timeout_ms=120000,progress_deadlines_arm64_el0_executed=True)
if memory:proof.update(allocator_region_max_bytes=67108864,large_owned_allocation_arm64_executed=True,process_memory_limit_bytes=268435456)
if budget:proof.update(process_memory_limit_bytes=536870912,ordinary_process_memory_limit_bytes=268435456,browser_budget_arm64_el0_executed=True,allocator_pool_fallback_validated=True)
DEST.write_text(json.dumps(proof,indent=2)+'\n');shutil.copytree(BASE,PACKAGE)
for target,source in {APP+'Dihscover.sacx':OUT/'frontend/Dihscover.sacx',APP+'WebCore.bin':OUT/'frontend/Dihscover-WebCore.bin',
 APP+'ENGINE.json':OUT/'frontend/Dihscover-engine.json',APP+'INTEGRATION.json':DEST,APP+'WEBCORE.md':documentation,
 'OS/aa64/KERNEL.ELF':KERNEL/'OS/aa64/KERNEL.ELF','EFI/BOOT/BOOTAA64.EFI':KERNEL/'build/BOOTAA64.EFI','OS/aa64/STAGE2.EFI':KERNEL/'build/STAGE2.EFI'}.items():shutil.copy2(source,PACKAGE/target)
with zipfile.ZipFile(BASE/(APP+'SOURCES.zip')) as old,zipfile.ZipFile(PACKAGE/(APP+'SOURCES.zip'),'w',zipfile.ZIP_DEFLATED) as z:
    for name in old.namelist():
        if name not in changed:z.writestr(name,old.read(name))
    for name in sorted(changed):z.write(ROOT/name,name)
    z.write(DEST,DEST.relative_to(ROOT).as_posix())
if not progress or memory:
    with zipfile.ZipFile(PACKAGE/(APP+'KERNEL-SOURCES.zip'),'w',zipfile.ZIP_DEFLATED) as z:
        for name in sorted(meta['files']):z.write(KERNEL/name,name)
        z.writestr('SOURCE-MANIFEST.json',json.dumps(meta,indent=2)+'\n')
build=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+proof['kernel_sha256'][:12].upper()
(PACKAGE/'OS/System/BUILD.TXT').write_text('DihOS ARM64 Hyper-V build '+build+'\nSystem file hashes: 0:/manifest.json\n',encoding='ascii')
files=[{'path':p.relative_to(PACKAGE).as_posix(),'size':p.stat().st_size,'sha256':sha(p).upper()} for p in sorted(PACKAGE.rglob('*')) if p.is_file() and p.name!='manifest.json']
(PACKAGE/'manifest.json').write_text(json.dumps({'version':1,'build':build,'files':files},indent=2)+'\n',encoding='utf-8-sig')
check()
(OUT/'package-result.json').write_text(json.dumps({'package':str(PACKAGE),'build':build,'files':len(files),'vhd_updated':False},indent=2)+'\n')
