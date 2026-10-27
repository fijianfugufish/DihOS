"""Verify pinned browser sources, fonts and the two copies of the SACX ABI."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def tree_digest(relative):
    path=root/relative
    files=[path] if path.is_file() else sorted(p for p in path.rglob('*') if p.is_file())
    h=hashlib.sha256()
    for f in files:h.update(f.relative_to(root).as_posix().encode()+b'\0'+bytes.fromhex(digest(f)))
    return h.hexdigest()
def check():
    manifest=json.loads((root/'tools/browser-dependencies.lock.json').read_text())
    for relative,expected in manifest['trees'].items():
        if tree_digest(relative)!=expected:raise SystemExit('Browser dependency changed: '+relative)
    for relative,expected in manifest['files'].items():
        if digest(root/relative)!=expected:raise SystemExit('Browser dependency changed: '+relative)
    kernel=(root/'kernel/include/apps/sacx_api.h').read_text().replace('"apps/sacx_keys.h"','"sacx_keys.h"')
    if (root/'sdk/sacx/include/sacx_api.h').read_text()!=kernel:raise SystemExit('Kernel/SDK SACX API headers differ')
    print('PASS: pinned font libraries/resources, packaged font hashes and shared SACX ABI')
if __name__=='__main__':check()
