"""Build the connected MSE player separately from earlier tested artifacts."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[3];MEDIA=ROOT/'tools/webcore_dihos/media'
p=argparse.ArgumentParser();p.add_argument('--compile',action='store_true');p.add_argument('--build',action='store_true');p.add_argument('--jobs',type=int,choices=range(1,9),default=4);p.add_argument('--trace-media-states',action='store_true');args=p.parse_args()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
OUT=ROOT/'build/dihscover-mse-player-engine';OUT.mkdir(exist_ok=True)
video=json.loads((ROOT/'build/dihscover-video-engine/report.json').read_text());original=Path(video['source'])
source=original.parent/'source-dihos-mse-player-v1';build=original.parent/'build-dihos-mse-player-v1'
if not source.exists():
    print('Cloning isolated MSE player source',flush=True);subprocess.run(['cp','-a','--reflink=auto',str(original),str(source)],check=True)
edits={};inputs={}
def write(name,text):
    path=source/name
    if not path.exists() or path.read_text()!=text:path.write_text(text)
    edits[name]=sha(path)
def patch(name,before,after):
    text=(original/name).read_text();assert text.count(before)==1,name;write(name,text.replace(before,after))
patch('Source/cmake/OptionsDihOS.cmake','WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_MEDIA_SOURCE PRIVATE OFF)','WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_MEDIA_SOURCE PRIVATE ON)')
patch('Source/WebCore/platform/graphics/MediaPlayer.h','    Remote\n};','    Remote,\n    DihOS\n};')
patch('Source/WebCore/platform/MediaSample.h','        GStreamerSample,','        GStreamerSample,\n        DihOS,')
name='Source/WebCore/html/HTMLMediaElement.cpp';text=(original/name).read_text()
if args.trace_media_states:
    before='    bool playerPaused = player->paused();\n'
    assert text.count(before)==1
    text=text.replace(before,before+'''    if (shouldBePlaying != !playerPaused)
        WTFLogAlways("DihOS play state: want=%d backendPaused=%d paused=%d internal=%d ready=%u maxReady=%u ended=%d error=%d interaction=%d now=%.6f duration=%.6f backend=%.6f seeking=%d lastSeek=%.6f default=%.6f", shouldBePlaying, playerPaused, m_paused, m_pausedInternal, unsigned(m_readyState), unsigned(m_readyStateMaximum), endedPlayback(), stoppedDueToErrors(), pausedForUserInteraction(), currentMediaTime().toDouble(), durationMediaTime().toDouble(), player->currentTime().toDouble(), m_seeking, m_lastSeekTime.toDouble(), m_defaultPlaybackStartPosition.toDouble());
''')
write(name,text)
for name in ['SourceBufferDihOS.h','SourceBufferDihOS.cpp','MediaSourceDihOS.h','MediaSourceDihOS.cpp',
             'MediaPlayerDihOS.h','MediaPlayerDihOS.cpp','encoded_media.h','fragmented_mp4.h','sample_decoder.h','memory_video.h']:
    path=MEDIA/name;inputs[path.relative_to(ROOT).as_posix()]=sha(path);write('Source/WebCore/platform/dihos/'+name,path.read_text())
name='Source/WebCore/PlatformDihOS.cmake';write(name,(original/name).read_text()+'\nlist(APPEND WebCore_SOURCES platform/dihos/SourceBufferDihOS.cpp platform/dihos/MediaSourceDihOS.cpp)\n')
report={'source':str(source),'build':str(build),'platform_files':edits,'source_inputs':inputs,'steps':[],
        'builder_sha256':sha(Path(__file__)),'tracing_enabled':args.trace_media_states,
        'video_enabled':True,'media_source_enabled':True,'web_audio_enabled':False,'engine_configured':False,'engine_built':False,
        'player_compiled':False,'javascript_mse_executed':False,'youtube_validated':False,'shipping_package':False}
def save():(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label,command):
    with (OUT/(label+'.log')).open('w') as log:code=subprocess.call(list(map(str,command)),cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    report['steps'].append({'label':label,'argv':list(map(str,command)),'exit_code':code});save();print(label,code,flush=True)
    if code:raise SystemExit('Inspect '+str(OUT/(label+'.log')))
command=list(video['steps'][0]['argv']);command[command.index('-S')+1]=str(source);command[command.index('-B')+1]=str(build)
command=[a.replace('-DENABLE_MEDIA_SOURCE=OFF','-DENABLE_MEDIA_SOURCE=ON') for a in command]+['-DENABLE_MEDIA_SOURCE_IN_WORKERS=OFF']
run('configure',command)
config=(build/'cmakeconfig.h').read_text()
for line in ['#define ENABLE_MEDIA_SOURCE 1','#define ENABLE_VIDEO 1','#define ENABLE_MEDIA_SOURCE_IN_WORKERS 0','#define ENABLE_WEB_AUDIO 0']:assert line in config,line
report['config_sha256']=sha(build/'cmakeconfig.h');report['engine_configured']=True;save()
if args.compile:
    targets=['Source/WebCore/CMakeFiles/WebCore.dir/platform/dihos/'+name+'.cpp.obj' for name in ['MediaPlayerDihOS','MediaSourceDihOS','SourceBufferDihOS']]
    run('compile-player',['ninja','-C',build,'-j',str(args.jobs),*targets]);report['player_compiled']=True
    report['player_objects']={n:sha(build/n) for n in targets};save()
if args.build:
    run('compile',['ninja','-C',build,'-j',str(args.jobs),'WebCore','JavaScriptCoreJIT'])
    report['libraries']={str(p):sha(p) for p in sorted(build.glob('lib/*.a'))};report['engine_built']=True;save()
