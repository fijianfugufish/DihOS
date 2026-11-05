"""Seal the tested MSE candidate, retaining the last working package and VHD."""
from pathlib import Path
import datetime,hashlib,json,shutil,sys,zipfile
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/dihscover-mse-player'
BASE=ROOT/'build/dihscover-video-player-20261104'
PACKAGE=ROOT/'build/dihscover-mse-player-20261104'
DEST=ROOT/'docs/browser-evaluation/dihscover-mse-player-20261104.json'
APP='OS/System/Programs/Dihscover/'
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def check():
    proof=read(DEST);manifest=read(PACKAGE/'manifest.json');seen=set()
    for item in manifest['files']:
        p=(PACKAGE/item['path']).resolve()
        assert p.is_relative_to(PACKAGE.resolve()) and item['path'] not in seen
        seen.add(item['path']);assert p.stat().st_size==item['size'] and sha(p).upper()==item['sha256'],item['path']
    assert seen=={p.relative_to(PACKAGE).as_posix() for p in PACKAGE.rglob('*') if p.is_file() and p.name!='manifest.json'}
    for name,digest in proof['proof_files'].items():assert sha(ROOT/name)==digest,name
    with zipfile.ZipFile(PACKAGE/(APP+'SOURCES.zip')) as z:
        for name,digest in proof['source_hashes'].items():assert hashlib.sha256(z.read(name)).hexdigest()==digest,name
    assert read(PACKAGE/(APP+'INTEGRATION.json'))==proof
    for name,key in [('WebCore.bin','renderer_sha256'),('Dihscover.sacx','frontend_sha256')]:assert sha(PACKAGE/(APP+name))==proof[key]
    allowed={APP+name for name in ['WebCore.bin','Dihscover.sacx','ENGINE.json','INTEGRATION.json','WEBCORE.md','SOURCES.zip']}
    allowed.add('OS/System/BUILD.TXT')
    for item in read(BASE/'manifest.json')['files']:
        if item['path'] not in allowed:assert sha(PACKAGE/item['path']).upper()==item['sha256'],item['path']
    assert sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d'
    print('PASS: matched MSE package, archived sources, execution evidence and preserved kernel/resources; VHD unchanged')
if '--check' in sys.argv:check();raise SystemExit
assert not PACKAGE.exists() and not DEST.exists(),'Do not overwrite sealed packages'
previous=read(ROOT/'docs/browser-evaluation/dihscover-video-player-20261104.json')
for item in read(BASE/'manifest.json')['files']:assert sha(BASE/item['path']).upper()==item['sha256'],item['path']
renderer=read(OUT/'renderer/report.json');front=read(OUT/'frontend/Dihscover-engine.json')
engine=read(ROOT/'build/dihscover-mse-player-engine/report.json')
assert engine['engine_built'] and engine['media_source_enabled']
assert not engine['tracing_enabled'] and engine['builder_sha256']==sha(ROOT/'tools/webcore_dihos/media/configure_mse_player.py')
for name,digest in engine['source_inputs'].items():assert sha(ROOT/name)==digest,name
assert renderer['engine_report_sha256']==sha(ROOT/'build/dihscover-mse-player-engine/report.json')
video=read(OUT/'qemu/report.json');reg=read(OUT/'qemu-page/report.json');elf=read(OUT/'elf-tests/report.json')
for result in [video,reg,elf]:
    assert result['passed']
    for name,digest in result['inputs'].items():assert sha(ROOT/name)==digest,name
assert video['javascript_mse_executed'] and video['log_sha256']==sha(OUT/'qemu/run.log')
assert video['kernel_sha256']==sha(OUT/'qemu/kernel.elf')
for index in range(3):
    log=OUT/'qemu'/('repeat-'+str(index)+'.log');text=log.read_text()
    assert 'FAIL:' not in text and 'FATAL' not in text
    assert 'SACX MSE Page: JS append, owned tracks, red/blue painting, seek, timed playback, ended, abort and replacement OK' in text
    assert 'PASS: sparse quota/slot bounds' in text
assert video['renderer_report_sha256']==sha(OUT/'renderer/report.json') and video['elf_sha256']==renderer['video_page_sha256']
assert renderer['elf_sha256']==reg['image_test_sha256']==front['engine_sha256']==sha(OUT/'frontend/Dihscover-WebCore.bin')
assert renderer['fixture_elf_sha256']==reg['fixture_sha256']
assert front['frontend_sha256']==sha(OUT/'frontend/Dihscover.sacx')
for name,digest in front['sources'].items():assert sha(ROOT/name)==digest,name
sources=dict(previous['source_hashes']);changed={'tools/webcore_dihos/headless_page_fixture.cpp'}
changed|={p.relative_to(ROOT).as_posix() for p in (ROOT/'tools/webcore_dihos/media').rglob('*') if p.is_file() and '__pycache__' not in p.parts}
changed.add('docs/dihscover-mse-player.md')
for name in changed:sources[name]=sha(ROOT/name)
evidence={}
for name in ['renderer/report.json','qemu/report.json','qemu/run.log','qemu-page/report.json','qemu-page/run.log',
             'elf-tests/report.json','engine/report.json','frontend/Dihscover-engine.json']:
    p=OUT/name;evidence[p.relative_to(ROOT).as_posix()]=sha(p)
name='build/dihscover-mse-player-engine/report.json';evidence[name]=sha(ROOT/name)
for index in range(3):
    p=OUT/'qemu'/('repeat-'+str(index)+'.log');evidence[p.relative_to(ROOT).as_posix()]=sha(p)
proof={**previous,'scope':'Native H.264 fragmented-MP4 MediaSource playback and separate silent AAC timing',
    'source_hashes':sources,'proof_files':evidence,'renderer_sha256':renderer['elf_sha256'],
    'frontend_sha256':front['frontend_sha256'],'kernel_sha256':sha(BASE/'OS/aa64/KERNEL.ELF'),
    'media_source_implemented':True,'mse_arm64_el0_executed':True,'additional_playback_repeats':3,
    'audio_output_implemented':False,'youtube_guest_validated':False,'hyperv_guest_validated':False,
    'limits':['H.264 fragmented MP4 only; AAC-LC timing is silent, with no audio decoding/output',
        '8 MiB segment, 1 MiB initialization, 32 MiB per SourceBuffer; 32 packets/4 MiB decoder queue; 1920x1080 frames',
        'No WebM, AV1, DRM, MediaSource workers or demonstrated live YouTube/Shorts playback',
        'Owned offline ARM64 EL0 fixtures passed; native hardware and Hyper-V performance still need guest validation']}
DEST.write_text(json.dumps(proof,indent=2)+'\n');shutil.copytree(BASE,PACKAGE)
for target,source in {APP+'Dihscover.sacx':OUT/'frontend/Dihscover.sacx',APP+'WebCore.bin':OUT/'frontend/Dihscover-WebCore.bin',
    APP+'ENGINE.json':OUT/'frontend/Dihscover-engine.json',APP+'INTEGRATION.json':DEST,
    APP+'WEBCORE.md':ROOT/'docs/dihscover-mse-player.md'}.items():shutil.copy2(source,PACKAGE/target)
with zipfile.ZipFile(BASE/(APP+'SOURCES.zip')) as old,zipfile.ZipFile(PACKAGE/(APP+'SOURCES.zip'),'w',zipfile.ZIP_DEFLATED) as z:
    for name in old.namelist():
        if name not in changed:z.writestr(name,old.read(name))
    for name in sorted(changed):z.write(ROOT/name,name)
    z.write(DEST,DEST.relative_to(ROOT).as_posix())
build=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+proof['kernel_sha256'][:12].upper()
(PACKAGE/'OS/System/BUILD.TXT').write_text('DihOS ARM64 Hyper-V build '+build+'\nSystem file hashes: 0:/manifest.json\n',encoding='ascii')
files=[{'path':p.relative_to(PACKAGE).as_posix(),'size':p.stat().st_size,'sha256':sha(p).upper()} for p in sorted(PACKAGE.rglob('*')) if p.is_file() and p.name!='manifest.json']
(PACKAGE/'manifest.json').write_text(json.dumps({'version':1,'build':build,'files':files},indent=2)+'\n',encoding='utf-8-sig')
check()
(OUT/'package-result.json').write_text(json.dumps({'package':str(PACKAGE),'build':build,'files':len(files),'vhd_updated':False},indent=2)+'\n')
