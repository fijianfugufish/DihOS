"""Link the native OpenSSL integration test; execution is a separate gate."""
from pathlib import Path
import argparse,hashlib,json,platform,subprocess
parser=argparse.ArgumentParser();parser.add_argument('--unready',action='store_true');args=parser.parse_args()
ROOT=Path(__file__).resolve().parents[2]
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
crypto_path=ROOT/'build/webcore-openssl/report.json';crypto=json.loads(crypto_path.read_text())
base_path=ROOT/'build/user-jsc-native/resource-report.json';base=json.loads(base_path.read_text())
if not crypto.get('library_built') or crypto['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_openssl.py'):raise SystemExit('Build current native OpenSSL first')
for relative,digest in crypto['inputs'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Changed native crypto adapter: '+relative)
if sha(Path(crypto['library']))!=crypto['library_sha256']:raise SystemExit('Changed native OpenSSL archive')
for path,digest in base['libraries'].items():
    if sha(Path(path))!=digest:raise SystemExit('Changed native runtime dependency')
out=ROOT/('build/user-openssl-unready' if args.unready else 'build/user-openssl');out.mkdir(exist_ok=True)
source=ROOT/'tools/webcore_dihos/openssl/crypto_fixture.cpp';elf=out/'crypto.elf'
report={'target':'aarch64-none-elf','builder_sha256':sha(Path(__file__)),'fixture_sha256':sha(source),
        'profile':'unready' if args.unready else 'known-answers',
        'crypto_report_sha256':sha(crypto_path),'base_report_sha256':sha(base_path),
        'linked':False,'loader_accepted':False,'crypto_tested':False,'threading_tested':False,'steps':[]}
def save():(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label,argv):
    with (out/(label+'.log')).open('w') as stream:code=subprocess.call([str(v) for v in argv],cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT)
    report['steps'].append({'label':label,'argv':[str(v) for v in argv],'exit_code':code});save()
    if code:raise SystemExit(label+' failed; inspect '+str(out/(label+'.log')))
command=list(next(s for s in base['steps'] if s['label']=='compile-embedding')['argv'])
command[1:1]=['-DOPENSSL_SYS_DIHOS=1','-I'+str(ROOT/'tools/webcore_dihos/openssl'),*['-I'+v for v in crypto['includes']]]
if args.unready:command.insert(1,'-DDIHOS_CRYPTO_UNREADY=1')
command[command.index('-c')+1]=str(source);command[command.index('-o')+1]=str(out/'crypto.o')
run('compile',command)
link=list(next(s for s in base['steps'] if s['label']=='link')['argv'])
link[link.index(next(a for a in link if a.endswith('/embedding.o')))]=str(out/'crypto.o')
link=[a for a in link if not any(a.endswith('/'+v) for v in ['dispatch-queue.o','resource-load.o','RunLoopGeneric.o'])]
link=[a for a in link if not any(a.endswith('-lib'+v+'.a') for v in ['WTF','JavaScriptCore','JavaScriptCoreBytecode','bmalloc'])]
link.insert(link.index('--end-group'),crypto['library']);link[link.index('-o')+1]=str(elf)
report['linked_inputs']={a:sha(Path(a)) for a in link if a.endswith(('.o','.a','.ld'))}
run('link',link);report.update(linked=True,elf_sha256=sha(elf),elf_bytes=elf.stat().st_size);save()
run('inspector-build',['clang-22','-Ikernel/include','tools/inspect_user_elf.c','kernel/process/dihos_user_elf.c','-o',out/'inspect'])
run('loader-check',[out/'inspect',elf]);report['loader_accepted']=True;save()
run('imports',['aarch64-linux-gnu-nm','-u',elf])
if (out/'imports.log').read_text().strip():raise SystemExit('Native crypto ELF contains unresolved imports')
for path,digest in report['linked_inputs'].items():
    if sha(Path(path))!=digest:raise SystemExit('Input changed during link')
save();print('Native crypto fixture linked and loader accepted; execution still required',flush=True)
