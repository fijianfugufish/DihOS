"""Verify selected arithmetic-only compiler-rt sources at the LLVM SDK pin."""
import argparse,hashlib,json,subprocess
from pathlib import Path,PurePosixPath
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--fetch',action='store_true');args=parser.parse_args()
pin=json.loads((ROOT/'tools/user-libraries.lock.json').read_text())
repo=ROOT/'build/user-runtime-deps/llvm-project'
bundle=ROOT/('build/user-builtins-'+pin['version'])
directory='compiler-rt/lib/builtins/'
units=['divti3.c','udivti3.c','modti3.c','umodti3.c','udivmodti4.c','fixdfti.c','floattidf.c','clzti2.c']
units += ['addtf3.c','subtf3.c','multf3.c','divtf3.c','comparetf2.c','extenddftf2.c',
    'extendsftf2.c','fixtfdi.c','fixtfsi.c','floatditf.c','floatsitf.c','trunctfdf2.c',
    'trunctfsf2.c','floatunditf.c','floatunsitf.c','fixunstfdi.c','fixunstfsi.c','aarch64/fp_mode.c']
def git(*argv):return subprocess.check_output(['git','-C',str(repo),*argv],text=True,timeout=180).strip()
if git('rev-parse','HEAD')!=pin['commit']:raise SystemExit('Wrong LLVM source commit')
if args.fetch:
    subprocess.run(['git','-C',str(repo),'-c','core.longpaths=true','sparse-checkout','add',
        'compiler-rt/lib/builtins','compiler-rt/LICENSE.TXT','--skip-checks'],check=True,timeout=180)
records={}
for row in git('ls-tree','-r',pin['commit'],'compiler-rt/lib/builtins','compiler-rt/LICENSE.TXT').splitlines():
    meta,name=row.split('\t');path=PurePosixPath(name)
    if name!='compiler-rt/LICENSE.TXT' and name not in {directory+unit for unit in units} and not (str(path.parent)==directory.rstrip('/') and path.suffix in ('.h','.inc')):continue
    mode,kind,oid=meta.split()
    if mode!='100644' or kind!='blob':raise SystemExit('Unexpected builtin source entry')
    output=bundle/name
    if args.fetch and not output.exists():
        data=(repo/name).read_bytes()
        def blob(data):return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
        if blob(data)!=oid:data=data.replace(b'\r\n',b'\n')
        if blob(data)!=oid:raise SystemExit('Modified LLVM source: '+name)
        output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(data)
    if not output.is_file() or output.is_symlink() or output.is_junction():raise SystemExit('Missing/linked builtin source: '+name)
    data=output.read_bytes()
    if hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()!=oid:raise SystemExit('Modified builtin source: '+name)
    records[name]=hashlib.sha256(data).hexdigest()
if any(directory+name not in records for name in units):raise SystemExit('Incomplete arithmetic source selection')
if {p.relative_to(bundle).as_posix() for p in bundle.rglob('*') if p.is_file()}!=set(records):raise SystemExit('Unexpected builtin bundle files')
print(json.dumps({'bundle':str(bundle),'commit':pin['commit'],'files':records,
    'units':[directory+name for name in units],
    'tree_sha256':hashlib.sha256(json.dumps(records,sort_keys=True).encode()).hexdigest()}))
