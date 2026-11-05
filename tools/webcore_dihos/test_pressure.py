"""Reproduce allocator pressure at a fixed quota and validate the browser update."""
from pathlib import Path
import hashlib,json,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/dihscover-pressure-validation'
HOST=OUT/'allocator-pressure';HOST.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
with (HOST/'run.log').open('w') as log:
    subprocess.run(['sh','tools/test_user_allocator_host.sh'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
assert 'pressure reclaims two idle pools' in (HOST/'run.log').read_text()
archive=ROOT/'build/dihscover-damage-20261105/OS/System/Programs/Dihscover/SOURCES.zip'
with zipfile.ZipFile(archive) as z:old=z.read('sdk/sacx/user_runtime/allocator.cpp')
previous=json.loads((ROOT/'docs/browser-evaluation/dihscover-damage-20261105.json').read_text())
assert hashlib.sha256(old).hexdigest()==previous['source_hashes']['sdk/sacx/user_runtime/allocator.cpp']
(HOST/'prior-allocator.cpp').write_bytes(old)
command=['g++','-std=c++20','-g','-O1','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',
         '-DEXPECT_NO_PRESSURE_RECLAIM=1','-Isdk/sacx/user_runtime/include','-Ithird_party/tlsf',str(HOST/'prior-allocator.cpp'),
         'tools/user_allocator_host_tests.cpp','build/user-allocator/tlsf-host.o','-o',str(HOST/'prior-test')]
with (HOST/'control.log').open('w') as log:
    subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    subprocess.run([HOST/'prior-test'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
assert 'prior allocator retains idle pools and fails' in (HOST/'control.log').read_text()
names=['sdk/sacx/user_runtime/allocator.cpp','sdk/sacx/user_runtime/allocator_platform.cpp','sdk/sacx/user_runtime/include/dihos_user_allocator.h',
       'tools/user_allocator_host_tests.cpp','tools/test_user_allocator_host.sh','tools/webcore_dihos/test_pressure.py']
(HOST/'report.json').write_text(json.dumps({'passed':True,'sanitizers':['address','undefined'],
    'fixed_quota_control':True,'inputs':{n:sha(ROOT/n) for n in names},'prior_source_sha256':sha(HOST/'prior-allocator.cpp'),
    'prior_archive_sha256':sha(archive),'command':command,'log_sha256':sha(HOST/'run.log'),'control_log_sha256':sha(HOST/'control.log')},indent=2)+'\n')
subprocess.run(['python3','tools/webcore_dihos/test_damage.py','--output',str(OUT)],cwd=ROOT,check=True)
assert 'trusted browser gets 768 MiB; ordinary and network/window-granted apps retain 256 MiB' in (OUT/'qemu-page/run.log').read_text()
print('PASS: allocator pressure control, trusted-only browser budget and native browser/media regressions')
