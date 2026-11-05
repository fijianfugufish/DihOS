"""Validate incremental fMP4 against actual host and native DihOS decoders.

Uses a fresh artifact directory. Never rebuilds sealed video-player artifacts.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from make_fragment_fixture import fragmented
ROOT=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--native',action='store_true');p.add_argument('--output',type=Path);args=p.parse_args()
OUT=ROOT/'build/dihscover-streaming-validation'
if args.output:
    OUT=args.output.resolve();assert OUT.is_relative_to(ROOT/'build'),'Validation output must stay in the workspace build directory'
OUT.mkdir(exist_ok=True)
OLD=ROOT/'build/dihscover-media-validation'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
native=json.loads((OLD/'decoder-report.json').read_text())
fixture=json.loads((OLD/'fixture-report.json').read_text())
for n,d in native['libraries'].items():assert sha(Path(n))==d,n
for n,d in fixture['linked_inputs'].items():assert sha(Path(n))==d,n
source=Path(native['source']);build=Path(native['build']);host=source.parent/'host-reference'
init,segments=fragmented();data=init+b''.join(segments)
(OUT/'fragmented.mp4').write_bytes(data)
(OUT/'fragment_fixture.inc').write_text('static const unsigned char fragment_fixture[]={\n'+
    ',\n'.join(','.join(str(b) for b in data[i:i+24]) for i in range(0,len(data),24))+'\n};\n'+
    f'static const unsigned fragment_init_bytes={len(init)},fragment_first_bytes={len(segments[0])};\n')
report={'linked':False,'host_sanitizers_passed':False,'native_executed':False,'steps':[],
        'youtube_playback_supported':False,'webcore_sourcebuffer_integrated':False}
def save():(OUT/'fixture-report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label,command):
    with (OUT/(label+'.log')).open('w') as log:
        code=subprocess.call(list(map(str,command)),cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    report['steps'].append({'label':label,'argv':list(map(str,command)),'exit_code':code});save()
    print(label,code,flush=True)
    if code:raise SystemExit('Inspect '+str(OUT/(label+'.log')))
media=ROOT/'tools/webcore_dihos/media'
includes=['-I'+str(media),'-I'+str(OUT),'-I'+str(host),'-I'+str(source)]
for name in ['fragmented_mp4','memory_video']:
    run('host-'+name,['gcc','-O1','-g','-fsanitize=address,undefined',*includes,'-c',media/(name+'.c'),'-o',OUT/(name+'.host.o')])
run('host-fixture',['g++','-O1','-g','-fsanitize=address,undefined',*includes,media/'fragment_fixture.cpp',
    OUT/'fragmented_mp4.host.o',OUT/'memory_video.host.o','-Wl,--start-group',
    *[host/n/(n+'.a') for n in ['libavformat','libavcodec','libswscale','libavutil']],
    '-Wl,--end-group','-lm','-pthread','-o',OUT/'host-fixture'])
run('host-execute',[OUT/'host-fixture']);report['host_sanitizers_passed']=True;save()
includes=['-I'+str(media),'-I'+str(OUT),'-I'+str(build),'-I'+str(source)]
run('native-assembler',['clang-22',*native['compiler_flags'],*includes,'-c',media/'fragmented_mp4.c','-o',OUT/'fragmented-mp4.o'])
command=list(next(s['argv'] for s in fixture['steps'] if s['label']=='native-fixture'))
command[1:1]=['-I'+str(OUT)]
command[command.index('-c')+1]=str(media/'fragment_fixture.cpp');command[command.index('-o')+1]=str(OUT/'fragment-fixture.o')
run('native-fixture',command)
link=list(next(s['argv'] for s in fixture['steps'] if s['label']=='native-link'))
link[link.index(str(OLD/'video-fixture.o'))]=str(OUT/'fragment-fixture.o')
link=[a for a in link if a!=str(OLD/'native-clip.o')]
link.insert(link.index('--start-group'),str(OUT/'fragmented-mp4.o'))
link[link.index('-o')+1]=str(OUT/'video.elf');run('native-link',link)
run('loader-check',[OLD/'inspect',OUT/'video.elf'])
run('imports',['aarch64-linux-gnu-nm','-u',OUT/'video.elf']);assert not (OUT/'imports.log').read_text().strip()
report.update(linked=True,elf_sha256=sha(OUT/'video.elf'),
    linked_inputs={n:sha(Path(n)) for n in link if n.endswith(('.o','.a','.ld'))},
    inputs={p.relative_to(ROOT).as_posix():sha(p) for p in [media/'fragmented_mp4.c',media/'fragmented_mp4.h',
        media/'fragment_fixture.cpp',media/'make_fragment_fixture.py',media/'make_fixture.py',media/'memory_video.c',
        Path(__file__),OUT/'fragment_fixture.inc']})
save()
if args.native:
    original=media/'test_native.py';text=original.read_text()
    text=text.replace("ROOT=Path(__file__).resolve().parents[3]",'ROOT=Path('+repr(str(ROOT))+')')
    text=text.replace("BASE=ROOT/'build/dihscover-media-validation'",'BASE=Path('+repr(str(OUT))+')')
    text=text.replace('SACX video: owned MP4, H.264 frames, BGRA, timestamps, seek, cancellation and malformed input OK',
        'SACX fMP4: split appends, fragment decoding, timestamps, backpressure, abort and malformed input OK')
    text=text.replace('PASS: real native MP4/H.264 decoding and cleanup in DihOS ARM64 EL0; no browser playback claimed',
        'PASS: actual fragmented MP4/H.264 decoding in DihOS ARM64 EL0; YouTube not yet supported')
    script=OUT/'run-native.py';script.write_text(text)
    run('native-execute',[sys.executable,script])
    execution=json.loads((OUT/'qemu/report.json').read_text());assert execution['passed']
    report.update(native_executed=True,native_report_sha256=sha(OUT/'qemu/report.json'))
    save()
print('PASS: incremental fMP4 tests; native execution='+str(report['native_executed']),flush=True)
