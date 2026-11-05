"""Resume the cookie build after implementation-only fixes.

Rebuilds both cookie implementation units and the resource fixture. Refuses
reuse if any other implementation/header changed. session_cookies.h belongs
only to the two rebuilt units. A clean build remains build_cookies.py.
"""
from pathlib import Path
import json,hashlib
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'build/dihscover-cookie-validation/renderer'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report=json.loads((OUT/'report.json').read_text())
for p,d in report['objects'].items():assert sha(Path(p))==d,p
for p,d in report['sources'].items():
 if Path(p).name not in ['SessionCookiesDihOS.cpp','webcore_resource_fixture.cpp','session_cookies.h','headless_page_fixture.cpp','browser_embedded.cpp']:assert sha(Path(p))==d,p
first=(OUT/'compile-headless.log').stat().st_mtime
for directory in [ROOT/'tools/webcore_dihos',ROOT/'tools/webcore_dihos/platform',ROOT/'sdk/sacx/include']:
 for p in directory.glob('*.h'):
  if p.name!='session_cookies.h':assert p.stat().st_mtime<=first,'Header changed: '+str(p)
base=json.loads((ROOT/'build/webcore-headless-page/report.json').read_text());client=json.loads((ROOT/'build/webcore-headless-page/compile-report.json').read_text())
for p,d in base['linked_inputs'].items():assert sha(Path(p))==d,p
frame_headers=ROOT/'build/dihscover-frames-validation/renderer/headers';overlay=OUT/'include/sdk';staged=OUT/'platform'
includes=['-I'+str(frame_headers),'-I'+str(frame_headers/'WebCore'),'-I'+str(OUT/'include'),'-I'+str(overlay),'-I'+str(staged)]
import shutil,subprocess,struct
code=(ROOT/'tools/webcore_dihos/build_cookies.py').read_text()
# Reuse the exact compile/link recipe, rather than maintaining different flags.
exec(code[code.index('def save():'):code.index('template=client')])
template=client['argv'];replacements={str(ROOT/'build/webcore-headless-page/headless.o'):str(OUT/'headless.o')}
compile_source(ROOT/'tools/webcore_dihos/headless_page_fixture.cpp',template,'headless')
compile_source(ROOT/'tools/webcore_dihos/browser_embedded.cpp',template,'browser_embedded')
for source in client['support_sources']:replacements[str(ROOT/'build/webcore-headless-page'/(Path(source).stem+'.o'))]=str(OUT/(Path(source).stem+'.o'))
overrides=[str(OUT/(name+'.o')) for name in ['WebCoreResourceLoad','ResourceHandleDihOS','ResourceLoad','ImageDecoderFactory','DihOSImageDecoder']]
report['resumed_with_unchanged_headers']=True
exec(code[code.index('cookie_command=list(template)'):])
