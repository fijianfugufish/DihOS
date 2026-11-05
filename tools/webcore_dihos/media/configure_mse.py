"""Compile the native SourceBuffer adapter in an isolated, consistent MSE build.

This does not enable MSE in a shipping package or claim player integration.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[3]
MEDIA=ROOT/'tools/webcore_dihos/media'
p=argparse.ArgumentParser();p.add_argument('--compile',action='store_true');p.add_argument('--jobs',type=int,choices=range(1,9),default=8);args=p.parse_args()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
OUT=ROOT/'build/dihscover-mse-engine';OUT.mkdir(exist_ok=True)
video=json.loads((ROOT/'build/dihscover-video-engine/report.json').read_text())
original=Path(video['source']);cache=original.parent
source=cache/'source-dihos-mse-v1';build=cache/'build-dihos-mse-v1'
if not source.exists():
    print('Cloning isolated MSE source',flush=True)
    subprocess.run(['cp','-a','--reflink=auto',str(original),str(source)],check=True)
edits={}
def write(name,text):
    target=source/name
    if not target.exists() or target.read_text()!=text:target.write_text(text)
    edits[name]=sha(target)
def patch(name,before,after):
    text=(original/name).read_text();assert text.count(before)==1,name
    write(name,text.replace(before,after))
patch('Source/cmake/OptionsDihOS.cmake','WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_MEDIA_SOURCE PRIVATE OFF)',
      'WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_MEDIA_SOURCE PRIVATE ON)')
patch('Source/WebCore/platform/graphics/MediaPlayer.h','    Remote\n};','    Remote,\n    DihOS\n};')
patch('Source/WebCore/platform/MediaSample.h','        GStreamerSample,','        GStreamerSample,\n        DihOS,')
for name in ['SourceBufferDihOS.h','SourceBufferDihOS.cpp','encoded_media.h','fragmented_mp4.h']:
    write('Source/WebCore/platform/dihos/'+name,(MEDIA/name).read_text())
name='Source/WebCore/PlatformDihOS.cmake'
write(name,(original/name).read_text()+'\nlist(APPEND WebCore_SOURCES platform/dihos/SourceBufferDihOS.cpp)\n')
# MSE adds a pure virtual load overload. Until the real player factory exists,
# capability probes continue rejecting MSE and this path fails explicitly.
name='Source/WebCore/platform/dihos/MediaPlayerDihOS.cpp'
text=(original/name).read_text();before='    void load(const URL&,const LoadOptions&) final;';assert text.count(before)==1
write(name,text.replace(before,before+'\n#if ENABLE(MEDIA_SOURCE)\n    void load(const URL&,const LoadOptions&,MediaSourcePrivateClient&) final { fail(MediaPlayer::NetworkState::FormatError,"Video: MSE player integration pending"); }\n#endif'))
report={'source':str(source),'build':str(build),'platform_files':edits,'steps':[],
        'sourcebuffer_compiled':False,'sourcebuffer_executed':False,'player_integrated':False,'youtube_supported':False,
        'source_inputs':{p.relative_to(ROOT).as_posix():sha(p) for p in [Path(__file__),MEDIA/'SourceBufferDihOS.h',MEDIA/'SourceBufferDihOS.cpp',MEDIA/'encoded_media.h',MEDIA/'fragmented_mp4.h']}}
def save():(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label,command):
    with (OUT/(label+'.log')).open('w') as log:
        code=subprocess.call(list(map(str,command)),stdout=log,stderr=subprocess.STDOUT,cwd=ROOT)
    report['steps'].append({'label':label,'argv':list(map(str,command)),'exit_code':code});save()
    print(label,code,flush=True)
    if code:raise SystemExit('Inspect '+str(OUT/(label+'.log')))
command=list(video['steps'][0]['argv']);command[command.index('-S')+1]=str(source);command[command.index('-B')+1]=str(build)
command=[a.replace('-DENABLE_MEDIA_SOURCE=OFF','-DENABLE_MEDIA_SOURCE=ON') for a in command]
run('configure',command)
config=(build/'cmakeconfig.h').read_text();assert '#define ENABLE_MEDIA_SOURCE 1' in config and '#define ENABLE_VIDEO 1' in config
report['config_sha256']=sha(build/'cmakeconfig.h');save()
if args.compile:
    target='Source/WebCore/CMakeFiles/WebCore.dir/platform/dihos/SourceBufferDihOS.cpp.obj'
    run('compile-sourcebuffer',['ninja','-C',build,'-j',str(args.jobs),target])
    report['sourcebuffer_compiled']=True;report['object_sha256']=sha(build/target);save()
    print('Compiled real WebCore SourceBuffer adapter; playback/execution still required',flush=True)
