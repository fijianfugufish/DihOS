"""Seal a diagnostic renderer update; never overwrite the tested parent package."""
from pathlib import Path
import datetime,hashlib,json,shutil,sys,zipfile
ROOT=Path(__file__).resolve().parents[2]
vm_tables='--vm-tables' in sys.argv
pressure='--pressure' in sys.argv
transitions='--transitions' in sys.argv
damage=transitions or pressure or '--damage' in sys.argv
css_size='--css-size' in sys.argv
pool_slots='--pool-slots' in sys.argv
indexeddb=damage or vm_tables or css_size or pool_slots or '--indexeddb' in sys.argv
BASE=ROOT/('build/dihscover-optional-trace-20261104' if indexeddb else 'build/dihscover-youtube-budget-20261104')
OUT=ROOT/('build/dihscover-indexeddb-validation' if indexeddb else 'build/dihscover-optional-trace-validation')
PACKAGE=ROOT/('build/dihscover-indexeddb-20261104' if indexeddb else 'build/dihscover-optional-trace-20261104')
DEST=ROOT/'docs/browser-evaluation'/(PACKAGE.name+'.json')
DOC=ROOT/('docs/dihscover-indexeddb.md' if indexeddb else 'docs/dihscover-optional-trace.md')
if pool_slots:
    BASE=ROOT/'build/dihscover-indexeddb-20261104'
    OUT=ROOT/'build/dihscover-pool-slots-validation'
    PACKAGE=ROOT/'build/dihscover-pool-slots-20261105'
    DEST=ROOT/'docs/browser-evaluation'/(PACKAGE.name+'.json')
    DOC=ROOT/'docs/dihscover-pool-slots.md'
if css_size:
    BASE=ROOT/'build/dihscover-pool-slots-20261105'
    OUT=ROOT/'build/dihscover-css-size-validation'
    PACKAGE=ROOT/'build/dihscover-css-size-20261105'
    DEST=ROOT/'docs/browser-evaluation'/(PACKAGE.name+'.json')
    DOC=ROOT/'docs/dihscover-css-size.md'
KERNEL=ROOT/'build/youtube-pool-slots-kernel/src'
KERNEL_META=ROOT/'build/youtube-pool-slots-kernel/source.json'
if vm_tables:
    BASE=ROOT/'build/dihscover-css-size-20261105'
    OUT=ROOT/'build/dihscover-vm-tables-validation'
    PACKAGE=ROOT/'build/dihscover-vm-tables-20261105'
    DEST=ROOT/'docs/browser-evaluation'/(PACKAGE.name+'.json')
    DOC=ROOT/'docs/dihscover-vm-tables.md'
    KERNEL=ROOT/'build/youtube-vm-tables-kernel/src'
    KERNEL_META=ROOT/'build/youtube-vm-tables-kernel/source.json'
if damage:
    BASE=ROOT/'build/dihscover-vm-tables-20261105'
    OUT=ROOT/'build/dihscover-damage-validation'
    PACKAGE=ROOT/'build/dihscover-damage-20261105'
    DEST=ROOT/'docs/browser-evaluation'/(PACKAGE.name+'.json')
    DOC=ROOT/'docs/dihscover-damage.md'
    KERNEL=ROOT/'build/youtube-damage-kernel/src'
    KERNEL_META=ROOT/'build/youtube-damage-kernel/source.json'
if pressure:
    BASE=ROOT/'build/dihscover-damage-20261105'
    OUT=ROOT/'build/dihscover-pressure-validation'
    PACKAGE=ROOT/'build/dihscover-pressure-20261105'
    DEST=ROOT/'docs/browser-evaluation'/(PACKAGE.name+'.json')
    DOC=ROOT/'docs/dihscover-pressure.md'
    KERNEL=ROOT/'build/youtube-pressure-kernel/src'
    KERNEL_META=ROOT/'build/youtube-pressure-kernel/source.json'
if transitions:
    BASE=ROOT/'build/dihscover-pressure-20261105'
    OUT=ROOT/'build/dihscover-transitions-validation'
    PACKAGE=ROOT/'build/dihscover-software-transitions-20261105'
    DEST=ROOT/'docs/browser-evaluation'/(PACKAGE.name+'.json')
    DOC=ROOT/'docs/dihscover-software-transitions.md'
    KERNEL=ROOT/'build/youtube-pressure-kernel/src'
    KERNEL_META=ROOT/'build/youtube-pressure-kernel/source.json'
kernel_update=pool_slots or vm_tables or (damage and not transitions)
APP='OS/System/Programs/Dihscover/'
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
changed={APP+n for n in ['WebCore.bin','Dihscover.sacx','ENGINE.json','INTEGRATION.json','WEBCORE.md','SOURCES.zip']}|{'OS/System/BUILD.TXT'}
if kernel_update:changed|={APP+'KERNEL-SOURCES.zip','OS/aa64/KERNEL.ELF','EFI/BOOT/BOOTAA64.EFI','OS/aa64/STAGE2.EFI'}
def manifest_check(package):
    m=read(package/'manifest.json');seen=set()
    for item in m['files']:
        path=(package/item['path']).resolve();assert path.is_relative_to(package.resolve())
        assert item['path'] not in seen;seen.add(item['path'])
        assert path.stat().st_size==item['size'] and sha(path).upper()==item['sha256'],item['path']
    assert seen=={p.relative_to(package).as_posix() for p in package.rglob('*') if p.is_file() and p.name!='manifest.json'}
def check():
    manifest_check(BASE);manifest_check(PACKAGE);proof=read(DEST)
    for n,h in proof['proof_files'].items():assert sha(ROOT/n)==h,n
    assert read(PACKAGE/(APP+'INTEGRATION.json'))==proof
    assert sha(PACKAGE/(APP+'WebCore.bin'))==proof['renderer_sha256']
    assert sha(PACKAGE/(APP+'Dihscover.sacx'))==proof['frontend_sha256']
    assert sha(PACKAGE/'OS/aa64/KERNEL.ELF')==proof['kernel_sha256']
    for item in read(BASE/'manifest.json')['files']:
        if item['path'] not in changed:assert sha(PACKAGE/item['path']).upper()==item['sha256'],item['path']
    with zipfile.ZipFile(PACKAGE/(APP+'SOURCES.zip')) as z:
        for n,h in proof['source_hashes'].items():assert hashlib.sha256(z.read(n)).hexdigest()==h,n
    if pool_slots or vm_tables or damage:
        with zipfile.ZipFile(PACKAGE/(APP+'KERNEL-SOURCES.zip')) as z:
            meta=json.loads(z.read('SOURCE-MANIFEST.json'))
            for n,h in meta['files'].items():assert hashlib.sha256(z.read(n)).hexdigest()==h,n
    assert sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d'
    print('PASS: renderer package, source archive and evidence verified; kernel and assets retained; VHD unchanged')
if '--check' in sys.argv:check();raise SystemExit
assert not PACKAGE.exists() and not DEST.exists(),'Never overwrite sealed packages'
manifest_check(BASE)
previous=read(ROOT/'docs/browser-evaluation'/(BASE.name+'.json'))
renderer=read(OUT/'renderer/report.json');front=read(OUT/'frontend/Dihscover-engine.json')
reports=['abort-test/report.json','qemu/report.json','qemu-page/report.json','elf-tests/report.json']
if indexeddb:reports.append('indexeddb-control/report.json')
if pool_slots:reports.append('allocator-host/report.json')
if vm_tables:reports+=['vm-error-test/report.json','vm-tables-control/report.json']
if damage:reports+=['window-host/report.json']
if pressure:reports+=['allocator-pressure/report.json']
if transitions:reports+=['view-transition-test/report.json','view-transition-control-test/report.json']
for name in reports:
    result=read(OUT/name);assert result['passed']
    for n,h in result['inputs'].items():assert sha(ROOT/n)==h,n
assert read(OUT/'abort-test/report.json')['real_arm64_el0']
assert read(OUT/'qemu/report.json')['elf_sha256']==renderer['video_page_sha256']
assert read(OUT/'qemu/report.json')['renderer_report_sha256']==sha(OUT/'renderer/report.json')
assert read(OUT/'qemu-page/report.json')['image_test_sha256']==renderer['elf_sha256']
assert front['engine_sha256']==renderer['elf_sha256']==sha(OUT/'frontend/Dihscover-WebCore.bin')
assert front['frontend_sha256']==sha(OUT/'frontend/Dihscover.sacx')
for n,h in front['sources'].items():assert sha(ROOT/n)==h,n
files=['sdk/sacx/user_runtime/libcxx/platform.cpp','sdk/sacx/user_runtime/include/dihos_user_abort_trace.h',
       'tools/user_abort_trace_tests.cpp','tools/webcore_dihos/optional_abort_fixture.cpp',
       'tools/webcore_dihos/test_optional_abort.py','tools/webcore_dihos/package_optional_trace.py',
       'tools/webcore_dihos/validate_optional_trace.py',
       'tools/webcore_dihos/media/build_renderer.py','docs/dihscover-optional-trace.md']
if indexeddb:
    files+=['tools/webcore_dihos/platform/DatabaseProviderDihOS.h','tools/webcore_dihos/platform/DatabaseProviderDihOS.cpp',
            'tools/webcore_dihos/indexeddb_provider.py','tools/webcore_dihos/headless_page_fixture.cpp',
            'tools/webcore_dihos/media/make_mse_fixture.py','tools/webcore_dihos/media/page_mse_fixture.h','docs/dihscover-indexeddb.md']
    assert 'SACX IndexedDB: factory, asynchronous open/delete errors, enumeration and context teardown OK' in (OUT/'qemu/run.log').read_text()
    assert read(OUT/'indexeddb-control/report.json')['expected_function']=='IDBConnectionProxy::IDBConnectionProxy'
if pool_slots:
    files+=['sdk/sacx/user_runtime/allocator.cpp','tools/user_allocator_host_tests.cpp',
            'kernel/include/process/dihos_user_memory.h','kernel/process/dihos_user_sparse.inc',
            'tools/user_runtime_qemu_tests.c','tools/webcore_dihos/test_large_allocator.py',
            'tools/user_runtime_qemu_start.S',
            'tools/webcore_dihos/prepare_youtube_kernel.py','docs/dihscover-pool-slots.md']
    assert read(OUT/'allocator-host/report.json')['slot_control']
    assert 'guest 0xa46bbf script allocation succeeds beside sixteen live pools' in (OUT/'allocator-host/run.log').read_text()
    assert 'PASS: 32 owned heap regions, bounded exhaustion and complete cleanup; sparse VM remains at 16 slots' in (OUT/'qemu/run.log').read_text()
    meta=read(KERNEL_META)
    for n,h in meta['files'].items():assert sha(KERNEL/n)==h,n
    with zipfile.ZipFile(BASE/(APP+'KERNEL-SOURCES.zip')) as z:old_meta=json.loads(z.read('SOURCE-MANIFEST.json'))
    for n,h in old_meta['files'].items():
        if n not in ['kernel/include/process/dihos_user_memory.h','kernel/process/dihos_user_sparse.inc']:assert meta['files'][n]==h,n
sources={**previous['source_hashes'],**{n:sha(ROOT/n) for n in files}}
if damage:
    extra=['sdk/sacx/include/sacx_user_window.h','kernel/process/dihos_user_window.c','kernel/process/dihos_user_sacx.c',
           'tools/user_window_tests.c','tools/user_sacx_tests.c','tools/user_runtime_qemu_tests.c',
           'tools/webcore_dihos/browser_embedded.cpp','tools/webcore_dihos/ResourceLoad.cpp','tools/webcore_dihos/ResourceLoad.h',
           'tools/webcore_dihos/media/test_mse_regressions.py','tools/webcore_dihos/prepare_youtube_kernel.py',
           'tools/webcore_dihos/test_damage.py','docs/dihscover-damage.md']
    extra+=['tools/browser_repaint_tests.cpp','tools/webcore_dihos/browser_repaint.h']
    if pressure:extra+=['sdk/sacx/user_runtime/allocator.cpp','sdk/sacx/user_runtime/allocator_platform.cpp',
        'sdk/sacx/user_runtime/include/dihos_user_allocator.h','tools/user_allocator_host_tests.cpp',
        'kernel/process/dihos_user_image.inc','tools/user_browser_budget_program.c',
        'tools/webcore_dihos/test_pressure.py','docs/dihscover-pressure.md',
        'docs/browser-evaluation/dihscover-pressure-fault-20261105.json']
    if transitions:extra+=['tools/webcore_dihos/software_view_transitions.py','tools/webcore_dihos/view_transition_fixture.h',
        'tools/webcore_dihos/test_view_transitions.py','tools/webcore_dihos/headless_page_fixture.cpp',
        'tools/webcore_dihos/headless_page_platform.cpp','docs/dihscover-software-transitions.md']
    files+=extra;sources.update({n:sha(ROOT/n) for n in extra})
    assert 'PASS: consent-shaped fetch chain:' in (OUT/'qemu-page/run.log').read_text()
    meta=read(KERNEL_META)
    for n,h in meta['files'].items():assert sha(KERNEL/n)==h,n
    with zipfile.ZipFile(BASE/(APP+'KERNEL-SOURCES.zip')) as z:old_meta=json.loads(z.read('SOURCE-MANIFEST.json'))
    for n,h in old_meta['files'].items():
        if n not in extra:assert meta['files'][n]==h,n
if css_size:
    extra=['tools/webcore_dihos/css_parser_fallback.py','tools/browser_fixtures/css_allocation.html',
           'tools/browser_fixtures/css_allocation.inc','tools/webcore_dihos/capture_youtube_styles.py','docs/dihscover-css-size.md']
    files+=extra;sources.update({n:sha(ROOT/n) for n in extra})
if vm_tables:
    extra=['tools/jsc_dihos/OSAllocatorDihOS.cpp','tools/jsc_dihos/vm_failure_report.h','tools/vm_failure_report_tests.cpp',
           'tools/webcore_dihos/vm_error_fixture.cpp','kernel/include/memory/aarch64_user_vm.h',
           'tools/user_runtime_qemu_tests.c','tools/webcore_dihos/prepare_youtube_kernel.py','docs/dihscover-vm-tables.md']
    files+=extra;sources.update({n:sha(ROOT/n) for n in extra})
    assert 'PASS: 256 scattered mappings exceed 192 tables with physical quota remaining' in (OUT/'qemu/run.log').read_text()
    assert 'PASS: 192-table control exhausts metadata before its byte quota' in (OUT/'vm-tables-control/native/qemu/run.log').read_text()
    meta=read(KERNEL_META)
    for n,h in meta['files'].items():assert sha(KERNEL/n)==h,n
    with zipfile.ZipFile(BASE/(APP+'KERNEL-SOURCES.zip')) as z:old_meta=json.loads(z.read('SOURCE-MANIFEST.json'))
    for n,h in old_meta['files'].items():
        if n!='kernel/include/memory/aarch64_user_vm.h':assert meta['files'][n]==h,n
evidence=['renderer/report.json','renderer/page.symbols.elf','frontend/Dihscover-engine.json',*reports,
          'abort-test/native/qemu/run.log','abort-test/symbolize.log','qemu/run.log','qemu-page/run.log']
if indexeddb:
    evidence+=['indexeddb-control/native/qemu/run.log','indexeddb-control/symbolize.log']
    if not damage:evidence+=['fault.json']
if pool_slots:evidence+=['allocator-host/run.log','allocator-host/control.log']
if css_size:evidence.append('captured-css/report.json')
if vm_tables:evidence+=['vm-error-test/native/qemu/run.log','vm-error-test/symbolize.log','vm-tables-control/native/qemu/run.log']
if pressure:evidence+=['allocator-pressure/run.log','allocator-pressure/control.log','allocator-pressure/prior-allocator.cpp']
if transitions:
    evidence+=['view-transition-test/native/qemu/run.log','view-transition-control-test/native/qemu/run.log',
               'renderer/platform/DocumentDihOS.cpp','renderer/platform/UnifiedDocumentDihOS.cpp']
    for name in ['DocumentDihOS.cpp','UnifiedDocumentDihOS.cpp']:
        sources['ENGINE-OVERRIDES/'+name]=sha(OUT/'renderer/platform'/name)
proof={**previous,'scope':'IndexedDB placeholder crash fixed using explicit asynchronous unavailable-storage completion' if indexeddb else 'Hardened optional abort diagnostics; underlying YouTube caller not yet identified',
       'source_hashes':sources,'proof_files':{(OUT/n).relative_to(ROOT).as_posix():sha(OUT/n) for n in evidence},
       'renderer_sha256':renderer['elf_sha256'],'frontend_sha256':front['frontend_sha256'],
       'parent_package':BASE.name,'hardening_enabled':True,'optional_abort_arm64_tested':True,
       'youtube_guest_validated':False,'hyperv_guest_validated':False}
if indexeddb:proof.update(indexeddb_unavailable_errors_arm64_tested=True,indexeddb_storage_implemented=False,indexeddb_fault=previous['indexeddb_fault'] if damage else read(OUT/'fault.json'))
if pool_slots:
    proof.update(scope='32 bounded allocator/owned-region slots; unchanged byte quotas and 16 sparse VM slots',
                 kernel_sha256=sha(KERNEL/'OS/aa64/KERNEL.ELF'),heap_region_slots=32,allocator_pool_slots=32,sparse_region_slots=16,
                 allocator_slot_control_tested=True,indexeddb_fault=previous['indexeddb_fault'],allocation_fault=read(OUT/'fault.json'))
    proof['proof_files'][KERNEL_META.relative_to(ROOT).as_posix()]=sha(KERNEL_META)
if css_size:
    assert 'CSS large sheet: 4 MiB rules, 24px SVG, hidden menu and bounded rejection OK' in (OUT/'qemu-page/run.log').read_text()
    styles=read(OUT/'captured-css/report.json')
    largest=max(styles['styles'],key=lambda s:s['size'])
    assert largest['status']=='200' and largest['size']>3*1024*1024
    assert sha(OUT/'captured-css'/largest['path'])==largest['sha256']
    proof.update(scope='48 MiB bounded CSS initial token reservation; large-sheet SVG sizing and visibility regression',
                 indexeddb_fault=previous['indexeddb_fault'],css_initial_reservation_limit_bytes=48*1024*1024,
                 captured_stylesheet=largest,css_large_sheet_arm64_tested=True)
if vm_tables:
    proof.update(scope='512 bounded private page tables with old-limit capacity control and operation-specific VM diagnostics',
                 kernel_sha256=sha(KERNEL/'OS/aa64/KERNEL.ELF'),private_page_table_limit=512,vm_error_diagnostics_arm64_tested=True,
                 vm_table_control_tested=True,indexeddb_fault=previous['indexeddb_fault'],vm_fault=read(OUT/'fault.json'))
    proof['proof_files'][KERNEL_META.relative_to(ROOT).as_posix()]=sha(KERNEL_META)
if damage:
    proof.update(scope='Bounded atomic damage uploads and 16 ms redraw coalescing; consent-shaped fetch regression and pending-request diagnostics',
                 kernel_sha256=sha(KERNEL/'OS/aa64/KERNEL.ELF'),region_upload_max_pixels=16384,
                 consent_flow_modeled_arm64_tested=True,live_consent_flow_validated=False)
    proof['proof_files'][KERNEL_META.relative_to(ROOT).as_posix()]=sha(KERNEL_META)
if pressure:
    assert read(OUT/'allocator-pressure/report.json')['fixed_quota_control']
    assert 'trusted browser gets 768 MiB; ordinary and network/window-granted apps retain 256 MiB' in (OUT/'qemu-page/run.log').read_text()
    proof.update(scope='Allocator idle-pool reclamation under pressure; trusted-only 768 MiB browser cap and allocation-free failure accounting',
                 browser_memory_limit_bytes=768*1024*1024,ordinary_memory_limit_bytes=256*1024*1024,
                 allocator_pressure_control_tested=True,allocation_fault=read(ROOT/'docs/browser-evaluation/dihscover-pressure-fault-20261105.json'))
if transitions:
    assert read(OUT/'view-transition-control-test/report.json')['expected_exit']==180
    assert read(OUT/'view-transition-test/report.json')['expected_exit']==124
    assert renderer['software_view_transitions']
    proof.update(scope='Software backend skips visual view transitions using native callback/promise semantics; cross-document visual transition opt-out',
                 software_view_transition_fallback_arm64_tested=True,unpatched_graphics_layer_exit_180_reproduced=True,
                 document_override=renderer['software_view_transitions'])
DEST.write_text(json.dumps(proof,indent=2)+'\n');shutil.copytree(BASE,PACKAGE)
for target,source in {APP+'WebCore.bin':OUT/'frontend/Dihscover-WebCore.bin',APP+'Dihscover.sacx':OUT/'frontend/Dihscover.sacx',
 APP+'ENGINE.json':OUT/'frontend/Dihscover-engine.json',APP+'INTEGRATION.json':DEST,APP+'WEBCORE.md':DOC}.items():shutil.copy2(source,PACKAGE/target)
if kernel_update:
    for n in ['OS/aa64/KERNEL.ELF','EFI/BOOT/BOOTAA64.EFI','OS/aa64/STAGE2.EFI']:
        source=KERNEL/('build/'+Path(n).name if n.endswith('.EFI') else n)
        shutil.copy2(source,PACKAGE/n)
    with zipfile.ZipFile(PACKAGE/(APP+'KERNEL-SOURCES.zip'),'w',zipfile.ZIP_DEFLATED) as z:
        for n in sorted(meta['files']):z.write(KERNEL/n,n)
        z.writestr('SOURCE-MANIFEST.json',json.dumps(meta,indent=2)+'\n')
with zipfile.ZipFile(BASE/(APP+'SOURCES.zip')) as old,zipfile.ZipFile(PACKAGE/(APP+'SOURCES.zip'),'w',zipfile.ZIP_DEFLATED) as z:
    for n in old.namelist():
        if n not in files and not (transitions and n.startswith('ENGINE-OVERRIDES/')):z.writestr(n,old.read(n))
    for n in files:z.write(ROOT/n,n)
    if transitions:
        for name in ['DocumentDihOS.cpp','UnifiedDocumentDihOS.cpp']:z.write(OUT/'renderer/platform'/name,'ENGINE-OVERRIDES/'+name)
    z.write(DEST,DEST.relative_to(ROOT).as_posix())
build=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+proof['kernel_sha256'][:12].upper()
(PACKAGE/'OS/System/BUILD.TXT').write_text('DihOS ARM64 Hyper-V build '+build+'\nSystem file hashes: 0:/manifest.json\n')
manifest=[{'path':p.relative_to(PACKAGE).as_posix(),'size':p.stat().st_size,'sha256':sha(p).upper()} for p in sorted(PACKAGE.rglob('*')) if p.is_file() and p.name!='manifest.json']
(PACKAGE/'manifest.json').write_text(json.dumps({'version':1,'build':build,'files':manifest},indent=2)+'\n',encoding='utf-8-sig')
check()
(OUT/'package-result.json').write_text(json.dumps({'package':str(PACKAGE),'build':build,'files':len(manifest),'vhd_updated':False},indent=2)+'\n')
