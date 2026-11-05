"""Pinned ARM64 Linux dependency/build probe, kept separate from DihOS images.

Run inside ARM64 Ubuntu. No configure checks are patched/stubbed to succeed.
The produced Linux binary is NOT a DihOS executable or a browser port.
"""
from pathlib import Path
import argparse, hashlib, json, os, platform, re, signal, subprocess, tarfile, tempfile, time

VERSION='2.54.2'
SHA256='f3284e19e47fcb5ac8c63c2e1c113dbd65b2423eb8a545f5b345707829004f56'
URL=f'https://wpewebkit.org/releases/wpewebkit-{VERSION}.tar.xz'
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
cache=Path.home()/'.cache/dihos-webkit'
parser.add_argument('--source',default=str(cache/f'wpewebkit-{VERSION}'))
parser.add_argument('--build-root',default=str(cache/'build'))
parser.add_argument('--port',choices=['JSCOnly','WPE'],default='JSCOnly')
parser.add_argument('--build',action='store_true')
parser.add_argument('--jobs',type=int,default=2)
parser.add_argument('--timeout',type=int,default=600)
args=parser.parse_args()
if platform.machine() not in ('aarch64','arm64'):raise SystemExit('Use a native ARM64 build environment')
if not 1<=args.jobs<=4 or not 1<=args.timeout<=1800:raise SystemExit('Invalid bounded build limits')
output=root/'build/browser-engine-arm64';output.mkdir(parents=True,exist_ok=True)
archive=output/f'wpewebkit-{VERSION}.tar.xz'
if hashlib.sha256(archive.read_bytes()).hexdigest()!=SHA256:raise SystemExit('Source archive checksum mismatch')
source=Path(args.source).resolve()
if not source.exists():
    source.parent.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='dihos-wpe-extract-',dir=source.parent))
    with tarfile.open(archive,'r:xz') as tar:
        for entry in tar.getmembers():
            if Path(entry.name).parts[0]!=f'wpewebkit-{VERSION}':raise SystemExit('Unexpected archive root')
        tar.extractall(stage,filter='data')
    (stage/f'wpewebkit-{VERSION}').rename(source);stage.rmdir()
    (source/'.dihos-archive-sha256').write_text(SHA256)
if (source/'.dihos-archive-sha256').read_text()!=SHA256:raise SystemExit('Source directory provenance mismatch')
build=Path(args.build_root).resolve()/args.port
record={'version':VERSION,'archive_url':URL,'archive_sha256':SHA256,'source':str(source),'build':str(build),
        'port':args.port,'host':platform.platform(),'machine':platform.machine(),'guest_engine':False,'commands':[]}
record['probe_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
record['fixture_sha256']=hashlib.sha256((root/'tools/browser_fixtures/jsc-runtime.js').read_bytes()).hexdigest()
for name,cmd in {'compiler':['g++','--version'],'cmake':['cmake','--version'],'ninja':['ninja','--version'],
                 'icu':['pkg-config','--modversion','icu-uc'],'ruby':['ruby','--version']}.items():
    try:record[name]=subprocess.check_output(cmd,text=True,stderr=subprocess.STDOUT).splitlines()[0]
    except (OSError,subprocess.CalledProcessError) as e:record[name]=str(e)
files=['Source/cmake/OptionsWPE.cmake','Source/cmake/OptionsJSCOnly.cmake','Source/cmake/WebKitCommon.cmake',
       'Source/WTF/wtf/PlatformOS.h','Source/WTF/wtf/PlatformHave.h']
record['source_files']={p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in files if (source/p).exists()}
report=output/(args.port.lower()+'-probe.json')
def save():report.write_text(json.dumps(record,indent=2))
def run(label,command,timeout):
    log=output/(args.port.lower()+'-'+label+'.log')
    step={'label':label,'argv':command,'log':str(log),'started':time.time()};record['commands'].append(step);save()
    with log.open('w') as stream:
        process=subprocess.Popen(command,cwd=root,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
        try:step['exit_code']=process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            step['timed_out']=True;os.killpg(process.pid,signal.SIGTERM)
            try:process.wait(timeout=10)
            except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
            step['exit_code']=process.returncode
    step['elapsed_seconds']=round(time.time()-step['started'],3);save()
    print(json.dumps(step),flush=True)
    return step['exit_code']==0
configure=['cmake','-S',str(source),'-B',str(build),'-G','Ninja',f'-DPORT={args.port}',
           '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
if args.port=='JSCOnly':
    configure+=['-DENABLE_STATIC_JSC=ON','-DENABLE_JIT=OFF','-DENABLE_DFG_JIT=OFF','-DENABLE_FTL_JIT=OFF',
                '-DENABLE_C_LOOP=ON','-DENABLE_WEBASSEMBLY=OFF','-DENABLE_API_TESTS=OFF',
                '-DENABLE_SAMPLING_PROFILER=OFF',
                '-DENABLE_REMOTE_INSPECTOR=OFF','-DUSE_LIBBACKTRACE=OFF','-DUSE_SYSTEM_MALLOC=ON',
                '-DENABLE_UNIFIED_BUILDS=ON']
save()
if not run('configure',configure,180):raise SystemExit(1)
if args.build:
    target='jsc' if args.port=='JSCOnly' else 'WPEWebKit'
    if not run('build',['cmake','--build',str(build),'--target',target,'--parallel',str(args.jobs)],args.timeout):raise SystemExit(2)
    record['built_target']=target;save()
    if args.port=='JSCOnly':
        executable=build/'bin/jsc'
        record['executable']={'path':str(executable),'bytes':executable.stat().st_size,
                              'sha256':hashlib.sha256(executable.read_bytes()).hexdigest()}
        record['static_archives']={p.name:{'bytes':p.stat().st_size,'thin':p.open('rb').read(8)==b'!<thin>\n'}
                                   for p in (build/'lib').glob('*.a')}
        save()
        if not run('runtime',[str(executable),str(root/'tools/browser_fixtures/jsc-runtime.js')],30):raise SystemExit(3)
        runtime_log=(output/'jsconly-runtime.log').read_text()
        results=[json.loads(line.removeprefix('DIHOS_JSC_RESULT ')) for line in runtime_log.splitlines()
                 if line.startswith('DIHOS_JSC_RESULT ')]
        record['runtime_result']=results
        save()
        if len(results)!=1 or not results[0].get('language_pass'):raise SystemExit('Language assertions did not complete')
        for label,command in [('elf',['readelf','-h','-l','-d',str(executable)]),
                              ('imports',['nm','-D','--undefined-only',str(executable)])]:
            if not run(label,command,30):raise SystemExit(4)
        elf=(output/'jsconly-elf.log').read_text()
        record['linux_shared_libraries']=re.findall(r'Shared library: \[([^\]]+)\]',elf)
        record['linux_interpreter']=re.findall(r'Requesting program interpreter: ([^\]]+)\]',elf)
        imports=(output/'jsconly-imports.log').read_text().splitlines()
        record['dynamic_undefined_symbols']=[line.split()[-1] for line in imports if line.strip()]
        save()
print(str(report),flush=True)
