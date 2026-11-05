"""Retain the working Hyper-V snapshot and overlay only the ELF fixup ceiling."""
from pathlib import Path
import hashlib,io,json,shutil,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[3]
BASE=ROOT/'build/rr/src';OUT=ROOT/'build/video-kernel/src'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
parent=ROOT/'build/rr/source.json';meta=json.loads(parent.read_text())
assert not OUT.exists(),'Use a fresh kernel snapshot; never overwrite a built snapshot'
archive=subprocess.check_output(['git','archive','--format=zip',meta['base']],cwd=ROOT)
with zipfile.ZipFile(io.BytesIO(archive)) as stream:
    for member in stream.infolist():
        target=OUT/member.filename
        assert target.resolve().is_relative_to(OUT.resolve())
        stream.extract(member,OUT)
for name,digest in meta['files'].items():
    source=Path(meta.get('source_overrides',{}).get(name,str(BASE/name)))
    assert sha(source)==digest,name
    target=OUT/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
header='kernel/include/process/dihos_user_elf.h'
before=(OUT/header).read_text();after=(ROOT/header).read_text()
assert '#define DIHOS_USER_ELF_MAX_RELOCATIONS 262144u' in before
assert '#define DIHOS_USER_ELF_MAX_RELOCATIONS 524288u' in after
for guard in ['DIHOS_USER_ELF_MAX_BYTES (128u*1024u*1024u)',
              'DIHOS_USER_ELF_MAX_IMAGE_BYTES (128u*1024u*1024u)',
              'DIHOS_USER_ELF_INLINE_RELOCATIONS 256u']:
    assert guard in before and guard in after
(OUT/header).write_text(after)
meta.update(path=str(OUT),source_overrides={},parent_snapshot_sha256=sha(parent),media_overlays=[header])
meta['files']={name:sha(OUT/name) for name in meta['files']}
(OUT.parent/'source.json').write_text(json.dumps(meta,indent=2)+'\n')
assert sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d'
print('Prepared isolated Hyper-V kernel source: larger relative-fixup ceiling; existing file/image and per-step bounds retained')
