"""Execute the actual JS MediaSource fixture in the native ARM64 kernel."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[3];MEDIA=ROOT/'tools/webcore_dihos/media'
p=argparse.ArgumentParser();p.add_argument('--output',type=Path);args=p.parse_args()
OUT=(ROOT/args.output).resolve() if args.output else ROOT/'build/dihscover-mse-player'
assert OUT.is_relative_to(ROOT/'build');OUT.mkdir(exist_ok=True)
(OUT/'qemu/report.json').unlink(missing_ok=True)
subprocess.run([sys.executable,ROOT/'tools/webcore_dihos/prepare_large_test_inputs.py','--output',OUT],cwd=ROOT,check=True)
template=(MEDIA/'test_page.py').read_text()
template=template.replace('ROOT=Path(__file__).resolve().parents[3]','ROOT=Path('+repr(str(ROOT))+')',1)
template=template.replace("BASE=ROOT/'build/dihscover-video-player'",'BASE=Path('+repr(str(OUT))+')')
template=template.replace('def run(name,args,timeout=180):','def run(name,args,timeout=300):')
template=template.replace("(OUT/'report.json').write_text", "assert 'SACX MSE Page: JS append, owned tracks, red/blue painting, seek, timed playback, ended, abort and replacement OK' in log\n(OUT/'report.json').write_text",1)
runner=OUT/'run-mse-page.py';runner.write_text(template)
subprocess.run([sys.executable,runner],cwd=ROOT,check=True)
path=OUT/'qemu/report.json';report=json.loads(path.read_text())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report.update(javascript_mse_executed=True,youtube_validated=False)
for p in [Path(__file__),MEDIA/'test_page.py',MEDIA/'page_mse_fixture.h',MEDIA/'make_mse_fixture.py',runner,OUT/'page_mse_fixture.html']:
    report['inputs'][p.relative_to(ROOT).as_posix()]=sha(p)
path.write_text(json.dumps(report,indent=2)+'\n')
print('PASS: native JavaScript MediaSource/player playback and cancellation; no live YouTube result claimed')
