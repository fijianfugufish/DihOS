"""Create a verified header view; never edit the engine cache or existing aliases."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[2]
config=json.loads((ROOT/'build/webcore-engine-openssl/report.json').read_text())
src=Path(config['build'])/'WebCore/PrivateHeaders/WebCore'
dst=ROOT/'build/dihscover-frames-validation/renderer/headers/WebCore';dst.mkdir(parents=True,exist_ok=True)
for p in src.iterdir():
 target=dst/p.name
 if p.name=='EmptyFrameLoaderClient.h':
  text=p.read_text();needle='RefPtr<LocalFrame> createFrame(const AtomString&, HTMLFrameOwnerElement&) final;';assert text.count(needle)==1
  text=text.replace(needle,'RefPtr<LocalFrame> createFrame(const AtomString&, HTMLFrameOwnerElement&) override;')
  if target.exists():assert not target.is_symlink() and target.read_text()==text
  else:target.write_text(text)
 elif target.is_symlink():assert target.readlink()==p
 else:
  assert not target.exists();target.symlink_to(p,target_is_directory=p.is_dir())
print('PASS: unchanged cache and exact DihOS child-frame header hook')
