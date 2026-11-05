"""Compile pinned ICU sources for native SACX; no Linux target libraries.

Run the JSC target inventory first to prepare and verify its freestanding SDK.
This script reuses those recorded compiler arguments, never a Linux config.h.
An inventory failure is not a completed dependency or a guest test.
"""
from pathlib import Path
import argparse, concurrent.futures, hashlib, json, os, platform, signal, subprocess, tarfile, time

ROOT=Path(__file__).resolve().parents[1]
SCRIPT_SHA=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
PIN={'version':'78.2','archive':'icu4c-78.2-sources.tgz',
     'url':'https://github.com/unicode-org/icu/releases/download/release-78.2/icu4c-78.2-sources.tgz',
     'sha256':'3e99687b5c435d4b209630e2d2ebb79906c984685e78635078b672e03c89df35'}
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
parser=argparse.ArgumentParser()
parser.add_argument('--cache',type=Path,default=Path.home()/'.cache/dihos-jsc-target')
parser.add_argument('--all',action='store_true',help='compile every common/i18n source from the upstream lists')
parser.add_argument('--inventory',action='store_true',help='return success after recording unsupported sources')
parser.add_argument('--sdk-report',type=Path,default=ROOT/'build/jsc-dihos/report.json',help='completed native SDK inventory, optionally retained before an engine retry')
args=parser.parse_args()
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
cache=args.cache.resolve();cache.mkdir(parents=True,exist_ok=True)
out=ROOT/'build/icu-dihos';out.mkdir(parents=True,exist_ok=True)
archive=ROOT/'build/browser-engine-arm64'/PIN['archive']
if not archive.exists():raise SystemExit('Download the pinned official archive first: '+PIN['url'])
if sha(archive)!=PIN['sha256']:raise SystemExit('ICU archive digest mismatch')
canonical=cache/'icu'
if not canonical.exists():
    with tarfile.open(archive) as tar:
        if any(m.name!='icu' and not m.name.startswith('icu/') for m in tar):raise SystemExit('Unexpected archive path')
        tar.extractall(cache,filter='data')
source_hashes={}
with tarfile.open(archive) as tar:
    for member in tar:
        if member.name!='icu' and not member.name.startswith('icu/'):raise SystemExit('Unexpected archive path')
        if member.isfile():
            path=cache/member.name
            expected=hashlib.sha256(tar.extractfile(member).read()).hexdigest()
            if path.is_symlink() or sha(path)!=expected:raise SystemExit('Modified ICU source: '+member.name)
            source_hashes[member.name]=expected
source=canonical/'source'
jsc_path=args.sdk_report
jsc=json.loads(jsc_path.read_text())
if not jsc.get('target_macros_verified') or not jsc.get('runtime_thread_link'):raise SystemExit('Complete the JSC native runtime inventory first')
for relative,digest in jsc['sdk_inputs'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('SDK changed; rerun JSC inventory: '+relative)
runtime=Path(jsc['runtime_archive']['path'])
if sha(runtime)!=jsc['runtime_archive']['sha256']:raise SystemExit('Runtime archive changed')
step=next(s for s in jsc['steps'] if s['label']=='runtime-runtime' and s['exit_code']==0)
compiler=step['argv'][0];flags=step['argv'][1:step['argv'].index('-O2')]
profile=ROOT/'tools/jsc_dihos/icu-platform.h';profile_sha=sha(profile)
locale_header=ROOT/'tools/jsc_dihos/icu-locale.h';locale_sha=sha(locale_header)
settings=['-include',str(profile),'-frtti','-ffunction-sections','-fdata-sections',
          '-I'+str(source/'common'),'-I'+str(source/'i18n')]
units={'common':['uchar.cpp','utf_impl.cpp','umutex.cpp','cmemory.cpp','ustring.cpp',
                 'normalizer2.cpp','ucasemap.cpp','putil.cpp','udata.cpp'],
       'i18n':['ucol.cpp','number_decimalquantity.cpp','datefmt.cpp']}
if args.all:
    units={component:(source/component/'sources.txt').read_text().split() for component in units}
sdk_digest=hashlib.sha256(json.dumps(jsc['sdk_inputs'],sort_keys=True).encode()).hexdigest()
identity=hashlib.sha256((PIN['sha256']+profile_sha+sdk_digest+json.dumps(flags+settings)).encode()).hexdigest()[:12]
build=cache/('icu-build-'+identity);build.mkdir(exist_ok=True)
report={'format':1,'pin':PIN,'target':'aarch64-none-elf','compiler':jsc['compiler'],
        'probe_sha256':SCRIPT_SHA,'profile_sha256':profile_sha,'locale_header_sha256':locale_sha,'jsc_report_sha256':sha(jsc_path),
        'sdk_inputs':jsc['sdk_inputs'],'runtime_archive':jsc['runtime_archive'],
        'source_tree_sha256':hashlib.sha256(json.dumps(source_hashes,sort_keys=True).encode()).hexdigest(),
        'settings':settings,'all_sources':args.all,'libraries_built':False,'guest_validated':False,'units':[]}
def compile_unit(item):
    component,name=item
    path=source/component/name
    obj=build/(component+'-'+name+'.o');log=out/(component+'-'+name+'.log')
    argv=[compiler,*flags,*settings,'-DU_'+('COMMON' if component=='common' else 'I18N')+'_IMPLEMENTATION',
          '-O2','-ferror-limit=6','-fno-color-diagnostics','-c',str(path),'-o',str(obj)]
    local_key=''
    if name=='putil.cpp':
        position=argv.index('-c');argv[position:position]=['-include',str(locale_header)];local_key=locale_sha
    key=hashlib.sha256((sha(path)+json.dumps(argv)+sdk_digest+profile_sha+local_key).encode()).hexdigest()
    metadata=obj.with_suffix('.json')
    if metadata.exists() and obj.exists() and log.exists():
        cached=json.loads(metadata.read_text())
        if cached.get('key')==key and cached['object_sha256']==sha(obj) and cached['log_sha256']==sha(log):
            cached.update(cached=True,seconds=0)
            print(json.dumps({'unit':component+'/'+name,'exit_code':0,'cached':True}),flush=True)
            return cached
    start=time.monotonic()
    with log.open('w') as stream:
        process=subprocess.Popen(argv,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
        try:code=process.wait(timeout=120)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid,signal.SIGKILL);process.wait();code=124
    text=log.read_text(errors='replace')
    result={'component':component,'unit':name,'source_sha256':sha(path),'argv':argv,
            'exit_code':code,'seconds':round(time.monotonic()-start,3),'log_sha256':sha(log),
            'errors':[line for line in text.splitlines() if 'error:' in line]}
    if not code:
        result.update(object=str(obj),object_sha256=sha(obj),key=key)
        metadata.write_text(json.dumps(result))
    print(json.dumps({'unit':component+'/'+name,'exit_code':code}),flush=True)
    return result
jobs=[(component,name) for component,names in units.items() for name in names]
for component,name in jobs:
    if not (source/component/name).is_file():raise SystemExit('Missing pinned ICU source: '+component+'/'+name)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    for result in pool.map(compile_unit,jobs):
        report['units'].append(result)
        if len(report['units'])%10==0:(out/'report.json').write_text(json.dumps(report,indent=2))
if args.all and all(r['exit_code']==0 for r in report['units']):
    report['archives']={}
    for component in units:
        library=build/('libicu'+('uc' if component=='common' else 'i18n')+'.a')
        subprocess.run(['aarch64-linux-gnu-ar','rcs',str(library),
                        *[r['object'] for r in report['units'] if r['component']==component]],check=True)
        report['archives'][component]={'path':str(library),'sha256':sha(library)}
    # Data registration and the final native link still must pass before install.
    report['libraries_built']=True
for relative,digest in jsc['sdk_inputs'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('SDK changed during ICU build')
if sha(profile)!=profile_sha or sha(locale_header)!=locale_sha or sha(runtime)!=jsc['runtime_archive']['sha256']:raise SystemExit('Target inputs changed during ICU build')
report['compiled']=sum(r['exit_code']==0 for r in report['units'])
report['failed']=len(jobs)-report['compiled']
(out/'report.json').write_text(json.dumps(report,indent=2))
print(json.dumps({'report':str(out/'report.json'),'compiled':report['compiled'],'failed':report['failed']}))
if not args.inventory and not report['libraries_built']:raise SystemExit(1)
