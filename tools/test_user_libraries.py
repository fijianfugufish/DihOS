"""Exercise source-bundle integrity checks without mutating the real bundle."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
(root/'build').mkdir(exist_ok=True)
fixture=Path(tempfile.mkdtemp(prefix='library-integrity-',dir=root/'build')).resolve()
assert fixture.is_relative_to((root/'build').resolve())
(fixture/'tools').mkdir()
shutil.copy2(root/'tools/prepare_user_libcxx.py',fixture/'tools/prepare_user_libcxx.py')
bundle=fixture/'build/user-libcxx-test'
bundle.mkdir(parents=True)
header=bundle/'test.h';header.write_bytes(b'header\n')
files={'test.h':hashlib.sha256(header.read_bytes()).hexdigest()}
digest=hashlib.sha256(json.dumps(files,sort_keys=True,separators=(',',':')).encode()).hexdigest()
lock={'version':'test','commit':'test-fixture','repository':'unused','tree_sha256':digest}
(fixture/'tools/user-libraries.lock.json').write_text(json.dumps(lock))
record=dict(lock,files=files)
(bundle/'SOURCE.JSON').write_text(json.dumps(record))
def check(success):
    result=subprocess.run([sys.executable,str(fixture/'tools/prepare_user_libcxx.py')],capture_output=True,text=True,timeout=30)
    assert (result.returncode==0)==success,(result.stdout,result.stderr)
check(True)
header.write_bytes(b'tampered\n');check(False)
header.write_bytes(b'header\n');(bundle/'injected.h').write_bytes(b'extra');check(False)
(bundle/'injected.h').unlink();record['commit']='wrong';(bundle/'SOURCE.JSON').write_text(json.dumps(record));check(False)
record['commit']=lock['commit'];record['files']={'test.h':'0'*64};(bundle/'SOURCE.JSON').write_text(json.dumps(record));check(False)
(bundle/'SOURCE.JSON').unlink();check(False)
print('PASS: pinned source verification rejects altered/missing manifests, modified headers, extra headers and commit mismatch')
