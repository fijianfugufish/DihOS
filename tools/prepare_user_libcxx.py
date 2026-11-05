"""Verify a pinned portable libc++ source bundle; fetching is explicit."""
from pathlib import Path
import argparse,hashlib,json,subprocess
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--fetch',action='store_true');args=parser.parse_args()
lock=json.loads((root/'tools/user-libraries.lock.json').read_text())
bundle=root/f'build/user-libcxx-{lock["version"]}'
if not bundle.resolve().is_relative_to((root/'build').resolve()):raise SystemExit('Dependency path escapes build')
if bundle.exists():
    for path in [bundle,*bundle.rglob('*')]:
        if path.is_symlink() or path.is_junction():raise SystemExit('Linked dependency entry rejected')
def sha(data):return hashlib.sha256(data).hexdigest()
def digest(files):return sha(json.dumps(files,sort_keys=True,separators=(',',':')).encode())
def run(argv):return subprocess.check_output(argv,cwd=root,text=True,timeout=120)
if not (bundle/'SOURCE.JSON').exists():
    if not args.fetch:raise SystemExit('Missing pinned libc++ bundle. Run: python tools/prepare_user_libcxx.py --fetch')
    repo=root/'build/user-runtime-deps/llvm-project'
    if not repo.exists():
        subprocess.run(['git','-c','core.longpaths=true','clone','--filter=blob:none','--depth','1','--sparse','--branch',f'llvmorg-{lock["version"]}',lock['repository'],str(repo)],cwd=root,check=True,timeout=180)
    if run(['git','-C',str(repo),'rev-parse','HEAD']).strip()!=lock['commit']:raise SystemExit('Wrong LLVM commit; retained checkout unchanged')
    subprocess.run(['git','-C',str(repo),'-c','core.longpaths=true','sparse-checkout','set','libcxx/include','libcxx/src'],cwd=root,check=True,timeout=180)
    rows=run(['git','-C',str(repo),'ls-tree','-r',lock['commit'],'libcxx/include','libcxx/src/algorithm.cpp','libcxx/src/string.cpp','libcxx/LICENSE.TXT']).splitlines()
    files={}
    for line in rows:
        meta,name=line.split('\t');mode,kind,oid=meta.split()
        if mode!='100644' or kind!='blob' or not name.startswith('libcxx/'):raise SystemExit('Unexpected dependency entry')
        data=(repo/name).read_bytes()
        def blob(b):return hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()
        if blob(data)!=oid:data=data.replace(b'\r\n',b'\n')
        if blob(data)!=oid:raise SystemExit('Modified source: '+name)
        path=bundle/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data);files[name]=sha(data)
    if digest(files)!=lock['tree_sha256']:raise SystemExit('Pinned source tree mismatch')
    (bundle/'SOURCE.JSON').write_text(json.dumps(dict(lock,files=files),indent=2)+'\n')
record=json.loads((bundle/'SOURCE.JSON').read_text())
files={p.relative_to(bundle).as_posix():sha(p.read_bytes()) for p in bundle.rglob('*') if p.is_file() and p!=bundle/'SOURCE.JSON'}
if record['commit']!=lock['commit'] or record['files']!=files or digest(files)!=lock['tree_sha256']:raise SystemExit('libc++ source verification failed')
print(json.dumps({'bundle':str(bundle),'commit':lock['commit'],'tree_sha256':lock['tree_sha256'],'files':len(files)}))
