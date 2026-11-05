"""Preserve the tested cookie kernel snapshot, overlay only startup diagnostics."""
from pathlib import Path
import hashlib,json,shutil,subprocess,zipfile,io
ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'build/stp/src';OUT=ROOT/'build/rr/src'
meta=json.loads((ROOT/'build/stp/source.json').read_text())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
overrides={'kernel/apps/sacx_browser_session.inc','kernel/process/dihos_user_loader.c','kernel/include/net/http_request_headers.h','kernel/include/apps/sacx_network_internal.h','sdk/sacx/include/sacx_http_limits.h','sdk/sacx/include/sacx_user_http_request.h','kernel/net/knet_usb.c'}
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
meta['parent_snapshot_sha256']=sha(ROOT/'build/stp/source.json')
meta['overlay_sha256']=hashlib.sha256(json.dumps(meta['files'],sort_keys=True).encode()).hexdigest()
(OUT.parent/'source.json').write_text(json.dumps(meta,indent=2)+'\n')
print('PASS: isolated kernel source snapshot; request limits, 16 KiB reads and renderer recovery overlays')
