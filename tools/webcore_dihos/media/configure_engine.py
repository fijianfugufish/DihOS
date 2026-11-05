"""Configure/build an isolated video-enabled native WebCore.

Never mixes ENABLE_VIDEO layouts with the proven disabled-video archives.
This is an experimental engine, not a shipping browser package.
"""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess
ROOT=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--build',action='store_true');p.add_argument('--player-only',action='store_true');p.add_argument('--jobs',type=int,choices=range(1,9),default=4);args=p.parse_args()
OUT=ROOT/'build/dihscover-video-engine';OUT.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
baseline_path=ROOT/'build/webcore-engine-openssl/report.json'
baseline=json.loads(baseline_path.read_text());original=Path(baseline['source'])
for name,digest in baseline['platform_files'].items():assert sha(original/name)==digest,name
cache=Path.home()/'.cache/dihos-webcore-native'
source=cache/'source-dihos-video-v1';build=cache/'build-dihos-video-v1'
if not source.exists():
    print('Copying isolated native engine sources',flush=True)
    shutil.copytree(original,source,symlinks=True)
options=source/'Source/cmake/OptionsDihOS.cmake'
text=(original/'Source/cmake/OptionsDihOS.cmake').read_text()
assert text.count('WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_VIDEO PRIVATE OFF)')==1
text=text.replace('WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_VIDEO PRIVATE OFF)',
                  'WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_VIDEO PRIVATE ON)')
if not options.exists() or options.read_text()!=text:options.write_text(text)
from engine_edits import stage
edits=stage(original,source,ROOT)
report={'target':'aarch64-none-elf','baseline_report_sha256':sha(baseline_path),
        'builder_sha256':sha(Path(__file__)),'source':str(source),'build':str(build),
        'video_enabled':True,'media_source_enabled':False,'web_audio_enabled':False,
        'engine_configured':False,'engine_built':False,'browser_playback_tested':False,
        'platform_files':edits,'engine_edits_sha256':sha(Path(__file__).with_name('engine_edits.py')),'steps':[]}
def save():(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label,argv):
    with (OUT/(label+'.log')).open('w') as log:
        code=subprocess.call(list(map(str,argv)),cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    report['steps'].append({'label':label,'argv':list(map(str,argv)),'exit_code':code});save()
    print(label,code,flush=True)
    if code:raise SystemExit('Inspect '+str(OUT/(label+'.log')))
command=list(baseline['steps'][0]['argv'])
command[command.index('-S')+1]=str(source);command[command.index('-B')+1]=str(build)
command=[a.replace('-DENABLE_VIDEO=OFF','-DENABLE_VIDEO=ON') for a in command]
run('configure',command)
config=(build/'cmakeconfig.h').read_text()
assert '#define ENABLE_VIDEO 1' in config
assert '#define ENABLE_MEDIA_SOURCE 0' in config
assert '#define ENABLE_WEB_AUDIO 0' in config
report.update(engine_configured=True,config_sha256=sha(build/'cmakeconfig.h'))
save()
if args.player_only:
    run('compile-player',['ninja','-C',build,'-j',str(args.jobs),'Source/WebCore/CMakeFiles/WebCore.dir/platform/dihos/MediaPlayerDihOS.cpp.obj'])
if args.build:
    run('compile',['ninja','-C',build,'-j',str(args.jobs),'WebCore','JavaScriptCoreJIT'])
    report['libraries']={str(p):sha(p) for p in sorted(build.glob('lib/*.a'))}
    report['engine_built']=True;save()
    print('Video-enabled engine libraries built; player/link/execution validation still required',flush=True)
