"""Allow a console override in a private header view; cached engine is read-only."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[2]
src=ROOT/'build/dihscover-frames-validation/renderer/headers/WebCore'
dst=ROOT/'build/dihscover-complex-sites-validation/renderer/headers/WebCore'
dst.mkdir(parents=True,exist_ok=True)
for p in src.iterdir():
    target=dst/p.name
    if p.name=='EmptyClients.h':
        original=p.read_text()
        needle='void addMessageToConsole(JSC::MessageSource, JSC::MessageLevel, const String&, unsigned, unsigned, const String&) final { }'
        assert original.count(needle)==1
        patched=original.replace(needle,needle.replace('final','override'))
        if target.exists():assert not target.is_symlink() and target.read_text()==patched
        else:target.write_text(patched)
        (dst.parent/'console-header.json').write_text(json.dumps({'original_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'patched_sha256':hashlib.sha256(target.read_bytes()).hexdigest()},indent=2)+'\n')
    elif target.is_symlink():assert target.readlink()==p
    else:assert not target.exists();target.symlink_to(p,target_is_directory=p.is_dir())
print('PASS: private console override; original headers and engine cache unchanged')
