"""Preserve the tested cookie kernel snapshot, overlay only startup diagnostics."""
from pathlib import Path
import hashlib,json,shutil,subprocess,zipfile,io
ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'build/capt/src';OUT=ROOT/'build/stp/src'
meta=json.loads((ROOT/'build/capt/source.json').read_text())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
overrides={'kernel/apps/sacx_browser_session.inc','sdk/sacx/include/sacx_browser_session.h'}
assert not (OUT/'OS/aa64/KERNEL.ELF').exists(),'Keep built snapshots immutable; use a fresh directory'
archive=subprocess.check_output(['git','archive','--format=zip',meta['base']],cwd=ROOT)
with zipfile.ZipFile(io.BytesIO(archive)) as z:
    for member in z.infolist():
        target=OUT/member.filename
        assert target.resolve().is_relative_to(OUT.resolve())
        if not target.exists():z.extract(member,OUT)
for name,digest in meta['files'].items():
    source=BASE/name
    assert sha(source)==digest,name
    target=OUT/name;target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(ROOT/name if name in overrides else source,target)
meta['path']=str(OUT)
meta['files']={n:sha(OUT/n) for n in meta['files']}
meta['startup_overlays']=sorted(overrides)
meta['parent_snapshot_sha256']=sha(ROOT/'build/capt/source.json')
meta['overlay_sha256']=hashlib.sha256(json.dumps(meta['files'],sort_keys=True).encode()).hexdigest()
(OUT.parent/'source.json').write_text(json.dumps(meta,indent=2)+'\n')
print('PASS: isolated kernel source snapshot; only startup-progress broker/header changed')
