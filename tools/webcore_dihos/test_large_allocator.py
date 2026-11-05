"""Record the actual allocator sanitizer run for the large-script candidate."""
from pathlib import Path
import argparse,hashlib,json,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=Path('build/dihscover-youtube-memory-validation'));p.add_argument('--slot-control',action='store_true');args=p.parse_args()
OUT=(ROOT/args.output/'allocator-host').resolve();assert OUT.is_relative_to(ROOT/'build');OUT.mkdir(parents=True,exist_ok=True)
slot_control=args.slot_control
args=['sh','tools/test_user_allocator_host.sh']
with (OUT/'run.log').open('w') as log:result=subprocess.run(args,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
assert not result.returncode,'Inspect allocator-host/run.log'
text=(OUT/'run.log').read_text();assert 'PASS: 24 MiB aligned allocation, 40 MiB realloc' in text and '30000 randomized operations' in text
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
names=['sdk/sacx/user_runtime/allocator.cpp','sdk/sacx/user_runtime/include/dihos_user_allocator.h',
 'sdk/sacx/user_runtime/include/dihos_user_tlsf.h','tools/user_allocator_host_tests.cpp','tools/test_user_allocator_host.sh',
 'third_party/tlsf/tlsf.c','third_party/tlsf/tlsf.h','tools/webcore_dihos/test_large_allocator.py']
control=None
if slot_control:
    assert 'guest 0xa46bbf script allocation succeeds beside sixteen live pools' in text
    archive=ROOT/'build/dihscover-indexeddb-20261104/OS/System/Programs/Dihscover/SOURCES.zip'
    with zipfile.ZipFile(archive) as z:data=z.read('sdk/sacx/user_runtime/allocator.cpp')
    parent=json.loads((ROOT/'docs/browser-evaluation/dihscover-indexeddb-20261104.json').read_text())
    assert hashlib.sha256(data).hexdigest()==parent['source_hashes']['sdk/sacx/user_runtime/allocator.cpp']
    old=OUT/'legacy-allocator.cpp';old.write_bytes(data)
    command=['g++','-std=c++20','-g','-O1','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',
             '-DEXPECT_LEGACY_POOL_FAILURE=1','-Isdk/sacx/user_runtime/include','-Ithird_party/tlsf',str(old),
             'tools/user_allocator_host_tests.cpp','build/user-allocator/tlsf-host.o','-o',str(OUT/'legacy-test')]
    with (OUT/'control.log').open('w') as log:
        subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        subprocess.run([OUT/'legacy-test'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    assert 'legacy sixteen live pools reject the guest script allocation without a backing attempt' in (OUT/'control.log').read_text()
    control={'archive_sha256':sha(archive),'source_sha256':sha(old),'log_sha256':sha(OUT/'control.log'),'argv':command}
(OUT/'report.json').write_text(json.dumps({'passed':True,'sanitizers':['address','undefined'],
 'inputs':{name:sha(ROOT/name) for name in names},'argv':args,'exit_code':result.returncode,'log_sha256':sha(OUT/'run.log'),'slot_control':control},indent=2)+'\n')
print('PASS: recorded large allocator sanitizer and concurrency tests')
