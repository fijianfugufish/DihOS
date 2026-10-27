"""Embed the unmodified, pinned NetSurf styles. --check verifies generated output."""
from pathlib import Path
import argparse
root=Path(__file__).resolve().parents[1]
output='/* Generated from the vendored NetSurf resources. */\n'
for name in ('default','quirks','internal','adblock'):
    text=(root/f'third_party/netsurf-all-3.11/netsurf/resources/{name}.css').read_text()
    if ')DIHSCSS"' in text:raise ValueError('raw-string delimiter collision')
    output+=f'static const char resource_{name}_css[]=R"DIHSCSS({text})DIHSCSS";\n'
target=root/'sdk/sacx/apps/dihscover/netsurf_resources.inc'
ap=argparse.ArgumentParser();ap.add_argument('--check',action='store_true');a=ap.parse_args()
if a.check:
    if target.read_text()!=output:raise SystemExit('Stale NetSurf resources; regenerate with tools/generate_browser_resources.py')
else:target.write_text(output)
