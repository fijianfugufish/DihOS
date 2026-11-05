"""Run the existing CSS/image/network/iframe suite with the video-enabled ABI."""
from pathlib import Path
import hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/dihscover-video-player';OUT.mkdir(exist_ok=True)
baseline=ROOT/'tools/webcore_dihos/test_css_parser.py'
proof=json.loads((ROOT/'docs/browser-evaluation/dihscover-css-parser-20261103.json').read_text())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert proof['source_hashes']['tools/webcore_dihos/test_css_parser.py']==sha(baseline)
text=baseline.read_text()
text=text.replace('ROOT=Path(__file__).resolve().parents[2]', 'ROOT=Path('+repr(str(ROOT))+')')
text=text.replace('build/dihscover-css-parser-validation','build/dihscover-video-player')
text=text.replace('-DUSER_TEST_ARENA_PAGES=65536','-DUSER_TEST_ARENA_PAGES=131072')
generated=OUT/'run-regressions.py';generated.write_text(text)
subprocess.run(['python3',str(generated)],cwd=ROOT,check=True)
print('PASS: existing browser regression suite executed with the video-enabled renderer',flush=True)
