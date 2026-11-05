"""Link the same decoder fixture for host sanitizers and real DihOS EL0."""
from pathlib import Path
import hashlib,json,shlex,subprocess
from make_fixture import clip
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/dihscover-media-validation'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
native=json.loads((OUT/'decoder-report.json').read_text())
assert native['libraries_built']
for path,digest in native['libraries'].items():assert sha(Path(path))==digest,path
build,source=Path(native['build']),Path(native['source'])
data=clip();assert data==(OUT/'red-blue.mp4').read_bytes()
hd=clip(1920,1080,2);assert hd==(OUT/'red-blue-1080.mp4').read_bytes()
(OUT/'video_fixture.inc').write_text('static const unsigned char video_fixture[]={\n'+
    ',\n'.join(','.join(str(b) for b in data[i:i+24]) for i in range(0,len(data),24))+'\n};\n'+
    'extern "C" const unsigned char video_hd_fixture['+str(len(hd))+'];\n')
assembly=OUT/'video_hd.S'
assembly.write_text('.section .rodata.video_hd_fixture,"a"\n.balign 16\n.global video_hd_fixture\nvideo_hd_fixture:\n.incbin "'+str(OUT/'red-blue-1080.mp4')+'"\n.section .note.GNU-stack,"",%progbits\n')
report={'decoder_report_sha256':sha(OUT/'decoder-report.json'),'steps':[],
        'native_executed':False,'host_sanitizers_passed':False,'linked':False}
def save():(OUT/'fixture-report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label,args):
    with (OUT/(label+'.log')).open('w') as log:
        code=subprocess.call(list(map(str,args)),cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    report['steps'].append({'label':label,'argv':list(map(str,args)),'exit_code':code});save()
    print(label,code,flush=True)
    if code:raise SystemExit('Inspect '+str(OUT/(label+'.log')))
includes=['-I'+str(ROOT/'tools/webcore_dihos/media'),'-I'+str(OUT),'-I'+str(build),'-I'+str(source)]
adapter=ROOT/'tools/webcore_dihos/media/memory_video.c'
fixture=ROOT/'tools/webcore_dihos/media/decoder_fixture.cpp'
host=source.parent/'host-reference'
host_includes=['-I'+str(host),'-I'+str(source),'-I'+str(OUT),'-I'+str(adapter.parent)]
run('host-adapter',['gcc','-O1','-g','-fsanitize=address,undefined',*host_includes,'-c',adapter,'-o',OUT/'host-adapter.o'])
run('host-clip',['gcc','-c',assembly,'-o',OUT/'host-clip.o'])
run('host-fixture',['g++','-O1','-g','-fsanitize=address,undefined',*host_includes,fixture,OUT/'host-adapter.o',
    OUT/'host-clip.o',
    '-Wl,--start-group',*[host/n/(n+'.a') for n in ['libavformat','libavcodec','libswscale','libavutil']],'-Wl,--end-group','-lm','-pthread','-o',OUT/'host-fixture'])
run('host-execute',[OUT/'host-fixture']);report['host_sanitizers_passed']=True;save()
flags=native['compiler_flags']
run('native-adapter',['clang-22',*flags,*includes,'-c',adapter,'-o',OUT/'memory-video.o'])
support=adapter.parent/'native_c_support.c'
run('native-support',['clang-22',*flags,*includes,'-c',support,'-o',OUT/'native-c-support.o'])
run('native-clip',['clang-22','--target=aarch64-none-elf','-c',assembly,'-o',OUT/'native-clip.o'])
base=json.loads((ROOT/'build/user-jsc-native/resource-report.json').read_text())
command=list(next(s['argv'] for s in base['steps'] if s['label']=='compile-embedding'))
command[1:1]=includes
command[command.index('-c')+1]=str(fixture);command[command.index('-o')+1]=str(OUT/'video-fixture.o')
run('native-fixture',command)
allocator=ROOT/'sdk/sacx/user_runtime/allocator.cpp'
allocator_command=list(command);allocator_command[1:1]=['-I'+str(allocator.parent/'include'),'-I'+str(ROOT/'third_party/tlsf')]
allocator_command[allocator_command.index('-c')+1]=str(allocator)
allocator_command[allocator_command.index('-o')+1]=str(OUT/'allocator.o')
run('native-allocator',allocator_command)
link=list(next(s['argv'] for s in base['steps'] if s['label']=='link'))
link[link.index(next(a for a in link if a.endswith('/embedding.o')))]=str(OUT/'video-fixture.o')
link=[a for a in link if not any(a.endswith('/'+v) for v in ['dispatch-queue.o','resource-load.o','RunLoopGeneric.o'])]
link=[a for a in link if not any(a.endswith('-lib'+v+'.a') for v in ['WTF','JavaScriptCore','JavaScriptCoreBytecode','bmalloc'])]
link.insert(link.index('--start-group'),str(OUT/'memory-video.o'))
link.insert(link.index('--start-group'),str(OUT/'native-c-support.o'))
link.insert(link.index('--start-group'),str(OUT/'native-clip.o'))
link.insert(link.index('--start-group'),str(OUT/'allocator.o'))
link[link.index('--end-group'):link.index('--end-group')]=list(native['libraries'])
link[link.index('-o')+1]=str(OUT/'video.elf')
run('native-link',link)
run('inspector-build',['clang-22','-Ikernel/include','tools/inspect_user_elf.c','kernel/process/dihos_user_elf.c','-o',OUT/'inspect'])
run('loader-check',[OUT/'inspect',OUT/'video.elf'])
run('imports',['aarch64-linux-gnu-nm','-u',OUT/'video.elf'])
assert not (OUT/'imports.log').read_text().strip()
report.update(linked=True,elf_sha256=sha(OUT/'video.elf'),
    inputs={str(p.relative_to(ROOT)):sha(p) for p in [adapter,support,allocator,fixture,Path(__file__),OUT/'video_fixture.inc',OUT/'red-blue-1080.mp4']},
    linked_inputs={a:sha(Path(a)) for a in link if a.endswith(('.o','.a','.ld'))})
save();print('Native decoder fixture linked and loader accepted; ARM64 execution still required',flush=True)
