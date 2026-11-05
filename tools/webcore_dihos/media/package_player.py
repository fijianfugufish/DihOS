"""Seal the executed video candidate without modifying prior packages or the VHD."""
from pathlib import Path
import datetime,hashlib,json,shutil,sys,tarfile,zipfile
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/dihscover-video-player'
BASE=ROOT/'build/dihscover-css-parser-20261103'
PACKAGE=ROOT/'build/dihscover-video-player-20261104'
DEST=ROOT/'docs/browser-evaluation/dihscover-video-player-20261104.json'
SRC=ROOT/'build/video-kernel/src'
APP='OS/System/Programs/Dihscover/'
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def check():
    proof=read(DEST);manifest=read(PACKAGE/'manifest.json');paths=set()
    for f in manifest['files']:
        p=(PACKAGE/f['path']).resolve()
        assert p.is_relative_to(PACKAGE) and f['path'] not in paths
        paths.add(f['path']);assert p.stat().st_size==f['size'] and sha(p).upper()==f['sha256'],f['path']
    assert paths=={p.relative_to(PACKAGE).as_posix() for p in PACKAGE.rglob('*') if p.is_file() and p.name!='manifest.json'}
    for key in ['source_hashes','proof_files']:
        for n,d in proof[key].items():assert sha(ROOT/n)==d,n
    for archive,field in [('SOURCES.zip','source_hashes'),('KERNEL-SOURCES.zip',None)]:
        with zipfile.ZipFile(PACKAGE/(APP+archive)) as z:
            hashes=proof[field] if field else json.loads(z.read('SOURCE-MANIFEST.json'))['files']
            for n,d in hashes.items():assert hashlib.sha256(z.read(n)).hexdigest()==d,n
    for n,k in [('WebCore.bin','renderer_sha256'),('Dihscover.sacx','frontend_sha256')]:assert sha(PACKAGE/(APP+n))==proof[k]
    assert sha(PACKAGE/'OS/aa64/KERNEL.ELF')==proof['kernel_sha256']
    assert read(PACKAGE/(APP+'INTEGRATION.json'))==proof
    allowed={'EFI/BOOT/BOOTAA64.EFI','OS/aa64/STAGE2.EFI','OS/aa64/KERNEL.ELF','OS/System/BUILD.TXT'}
    allowed|={APP+n for n in ['WebCore.bin','Dihscover.sacx','ENGINE.json','INTEGRATION.json','WEBCORE.md','SOURCES.zip','KERNEL-SOURCES.zip']}
    for f in read(BASE/'manifest.json')['files']:
        if f['path'] not in allowed:assert sha(PACKAGE/f['path']).upper()==f['sha256'],f['path']
    assert sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d'
    print('PASS: sealed package, matched binaries, sources, evidence and preserved resources; VHD unchanged')
if '--check' in sys.argv:check();raise SystemExit
assert not PACKAGE.exists() and not DEST.exists(),'Do not overwrite sealed packages'
previous=read(ROOT/'docs/browser-evaluation/dihscover-css-parser-20261103.json')
sources=dict(previous['source_hashes'])
changed={'tools/webcore_dihos/headless_page_fixture.cpp','tools/webcore_dihos/headless_page_platform.cpp',
         'tools/webcore_dihos/navigation_strategies.h','kernel/include/process/dihos_user_elf.h','tools/user_runtime_qemu_tests.c'}
for n,d in sources.items():
    if n not in changed:assert sha(ROOT/n)==d,n
for n in changed:sources[n]=sha(ROOT/n)
for p in (ROOT/'tools/webcore_dihos/media').rglob('*'):
    if p.is_file() and '__pycache__' not in p.parts:sources[p.relative_to(ROOT).as_posix()]=sha(p)
for n in ['tools/user_media_elf_tests.c','docs/dihscover-webcore-video.md'] :sources[n]=sha(ROOT/n)
evidence=dict(previous['proof_files'])
for n,d in evidence.items():assert sha(ROOT/n)==d,n
for n in ['renderer/report.json','qemu/report.json','qemu/run.log','qemu-page/report.json','qemu-page/run.log',
          'elf-tests/report.json','engine/report.json','frontend/Dihscover-engine.json']:
    p=OUT/n;evidence[p.relative_to(ROOT).as_posix()]=sha(p)
for n in ['build/video-kernel/source.json','build/dihscover-media-validation/decoder-report.json'] :evidence[n]=sha(ROOT/n)
video=read(OUT/'qemu/report.json');reg=read(OUT/'qemu-page/report.json');elf=read(OUT/'elf-tests/report.json')
for item in [video,reg,elf]:
    assert item['passed']
    for n,d in item['inputs'].items():assert sha(ROOT/n)==d,n
renderer=read(OUT/'renderer/report.json');front=read(OUT/'frontend/Dihscover-engine.json')
assert renderer['elf_sha256']==front['engine_sha256']==reg['image_test_sha256']==sha(OUT/'frontend/Dihscover-WebCore.bin')
assert video['renderer_report_sha256']==sha(OUT/'renderer/report.json') and video['elf_sha256']==renderer['video_page_sha256']
assert front['frontend_sha256']==sha(OUT/'frontend/Dihscover.sacx')
assert sha(SRC/'OS/aa64/KERNEL.ELF')=='422c20289477eed68b0cc68acfd37cf5e7f64e2eb5171baa5362acfb77ca1b27'
for f in read(BASE/'manifest.json')['files']:assert sha(BASE/f['path']).upper()==f['sha256'],f['path']
proof={**previous,'scope':'Native muted HTML MP4/H.264 playback; existing browser regression retained',
       'source_hashes':sources,'proof_files':evidence,'renderer_sha256':renderer['elf_sha256'],
       'frontend_sha256':front['frontend_sha256'],'kernel_sha256':sha(SRC/'OS/aa64/KERNEL.ELF'),
       'video_playback_implemented':True,'video_arm64_el0_executed':True,'media_source_implemented':False,
       'audio_output_implemented':False,'youtube_guest_validated':False,'hyperv_guest_validated':False,
       'media_input_max_bytes':8388608,'relative_relocation_limit':524288}
proof['limits']=[s for s in previous['limits'] if 'no video decoding/MediaSource/audio implementation' not in s]
proof['limits']+=['Complete-file MP4/H.264 only, 8 MiB input and 1920x1080 frames; no MediaSource, audio output or validated YouTube playback',
                  'Owned data-URL video executed in ARM64 EL0; live-network video and Hyper-V performance need guest validation']
DEST.write_text(json.dumps(proof,indent=2)+'\n');shutil.copytree(BASE,PACKAGE)
for target,source in {'EFI/BOOT/BOOTAA64.EFI':SRC/'build/BOOTAA64.EFI','OS/aa64/STAGE2.EFI':SRC/'build/STAGE2.EFI',
    'OS/aa64/KERNEL.ELF':SRC/'OS/aa64/KERNEL.ELF',APP+'Dihscover.sacx':OUT/'frontend/Dihscover.sacx',
    APP+'WebCore.bin':OUT/'frontend/Dihscover-WebCore.bin',APP+'ENGINE.json':OUT/'frontend/Dihscover-engine.json',
    APP+'INTEGRATION.json':DEST,APP+'WEBCORE.md':ROOT/'docs/dihscover-webcore-video.md'}.items():shutil.copy2(source,PACKAGE/target)
with zipfile.ZipFile(BASE/(APP+'KERNEL-SOURCES.zip')) as old,zipfile.ZipFile(PACKAGE/(APP+'KERNEL-SOURCES.zip'),'w',zipfile.ZIP_DEFLATED) as z:
    meta=json.loads(old.read('SOURCE-MANIFEST.json'));header='kernel/include/process/dihos_user_elf.h'
    for n,d in meta['files'].items():assert hashlib.sha256(old.read(n)).hexdigest()==d,n
    meta['files'][header]=sha(SRC/header);meta['media_snapshot_sha256']=sha(ROOT/'build/video-kernel/source.json')
    for n in old.namelist():
        if n=='SOURCE-MANIFEST.json':z.writestr(n,json.dumps(meta,indent=2)+'\n')
        elif n==header:z.write(SRC/header,n)
        else:z.writestr(n,old.read(n))
with zipfile.ZipFile(PACKAGE/(APP+'SOURCES.zip'),'w',zipfile.ZIP_DEFLATED) as z:
    for n in sorted(sources):z.write(ROOT/n,n)
    z.write(DEST,DEST.relative_to(ROOT).as_posix())
    for n in ['ffmpeg-9.0.2.tar.xz','ffmpeg-9.0.2.tar.xz.asc','ffmpeg-devel.asc','decoder-report.json']:
        z.write(ROOT/'build/dihscover-media-validation'/n,'ffmpeg/'+n)
with tarfile.open(ROOT/'build/dihscover-media-validation/ffmpeg-9.0.2.tar.xz') as t:
    member=next(m for m in t.getmembers() if m.name.endswith('/COPYING.LGPLv2.1'))
    (PACKAGE/(APP+'FFMPEG-LICENSE.txt')).write_bytes(t.extractfile(member).read())
build=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+proof['kernel_sha256'][:12].upper()
(PACKAGE/'OS/System/BUILD.TXT').write_text('DihOS ARM64 Hyper-V build '+build+'\nSystem file hashes: 0:/manifest.json\n',encoding='ascii')
files=[{'path':p.relative_to(PACKAGE).as_posix(),'size':p.stat().st_size,'sha256':sha(p).upper()} for p in sorted(PACKAGE.rglob('*')) if p.is_file() and p.name!='manifest.json']
(PACKAGE/'manifest.json').write_text(json.dumps({'version':1,'build':build,'files':files},indent=2)+'\n',encoding='utf-8-sig')
check()
(OUT/'package-result.json').write_text(json.dumps({'package':str(PACKAGE),'build':build,'files':len(files),'vhd_updated':False},indent=2)+'\n')
