"""Verify the selected libc++abi RTTI sources at the libc++ commit pin."""
from pathlib import Path
import argparse,hashlib,json,subprocess
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--fetch',action='store_true');args=parser.parse_args()
pin=json.loads((root/'tools/user-libraries.lock.json').read_text())
repo=root/'build/user-runtime-deps/llvm-project'
files=['libcxxabi/LICENSE.TXT','libcxxabi/include/__cxxabi_config.h','libcxxabi/include/cxxabi.h',
       'libcxxabi/src/private_typeinfo.cpp','libcxxabi/src/private_typeinfo.h',
       'libcxxabi/src/stdlib_typeinfo.cpp','libcxxabi/src/stdlib_exception.cpp',
       'libcxxabi/src/cxa_aux_runtime.cpp','libcxxabi/src/abort_message.h']
def git(*argv):return subprocess.check_output(['git','-C',str(repo),*argv],text=True,timeout=120).strip()
if git('rev-parse','HEAD')!=pin['commit']:raise SystemExit('Wrong LLVM source commit')
if args.fetch:
    subprocess.run(['git','-C',str(repo),'-c','core.longpaths=true','sparse-checkout','add',
                    'libcxxabi/include','libcxxabi/src'],check=True,timeout=180)
rows=git('ls-tree','-r',pin['commit'],*files).splitlines()
if len(rows)!=len(files):raise SystemExit('Incomplete pinned ABI source list')
bundle=root/('build/user-libcxxabi-'+pin['version']);records={}
for row in rows:
    meta,name=row.split('\t');mode,kind,oid=meta.split()
    if mode!='100644' or kind!='blob' or name not in files:raise SystemExit('Unexpected ABI source')
    path=bundle/name
    if args.fetch and not path.exists():
        data=(repo/name).read_bytes()
        def blob(data):return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
        if blob(data)!=oid:data=data.replace(b'\r\n',b'\n')
        if blob(data)!=oid:raise SystemExit('Modified LLVM source: '+name)
        path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    if not path.exists():raise SystemExit('Missing ABI bundle; run prepare_user_abi.py --fetch')
    if path.is_symlink() or path.is_junction():raise SystemExit('Linked ABI input')
    data=path.read_bytes()
    if hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()!=oid:raise SystemExit('Modified ABI bundle: '+name)
    records[name]=hashlib.sha256(data).hexdigest()
actual={p.relative_to(bundle).as_posix() for p in bundle.rglob('*') if p.is_file()}
if actual!=set(files):raise SystemExit('Unexpected ABI bundle files')
print(json.dumps({'bundle':str(bundle),'commit':pin['commit'],'files':records,
                  'tree_sha256':hashlib.sha256(json.dumps(records,sort_keys=True).encode()).hexdigest()}))
