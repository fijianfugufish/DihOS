"""Inventory pinned platform requirements; this is not a WebCore build proof."""
from pathlib import Path
import hashlib,json,re,tarfile
ROOT=Path(__file__).resolve().parents[2]
archive=ROOT/'build/browser-engine-arm64/wpewebkit-2.54.2.tar.xz'
pin='f3284e19e47fcb5ac8c63c2e1c113dbd65b2423eb8a545f5b345707829004f56'
if hashlib.sha256(archive.read_bytes()).hexdigest()!=pin:raise SystemExit('Pinned WebKit archive changed')
selected=['Source/cmake/OptionsWPE.cmake','Source/WebCore/PlatformWPE.cmake',
          'Source/WTF/wtf/RunLoop.cpp','Source/WTF/wtf/generic/RunLoopGeneric.cpp',
          'Source/WebCore/platform/SharedTimer.h','Source/WebCore/page/PageConfiguration.h',
          'Source/WebCore/platform/network/ResourceHandle.h']
sources={};declarations=[]
with tarfile.open(archive,'r|xz') as tar:
    for member in tar:
        relative=member.name.removeprefix('wpewebkit-2.54.2/')
        if relative not in selected:continue
        data=tar.extractfile(member).read();text=data.decode()
        sources[relative]={'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)}
        if relative.endswith('.cmake'):
            declarations += [{'source':relative,'line':i,'declaration':line.strip()}
                             for i,line in enumerate(text.splitlines(),1) if re.search(r'find_package\(',line)]
if set(sources)!=set(selected):raise SystemExit('Pinned platform input absent: '+str(set(selected)-sources.keys()))
report={'version':'2.54.2','archive_sha256':pin,'source_hashes':sources,
        'cmake_dependency_declarations':declarations,
        'declaration_scope':'Includes conditionally selected features; this is not a resolved dependency graph.',
        'native_webcore_configured':False,'native_webcore_compiled':False,'dom_executed':False,
        'host_engine_reused':False,'native_event_loop':'Generic WTF RunLoop over SACX native threading/conditions',
        'next_port_boundaries':['Custom native WebCore feature/platform configuration',
            'Checked asynchronous resource service and process IPC',
            'Fonts, image decoding and software graphics backend',
            'Frame/Page clients, DOM bindings and input/surface integration'],
        'shipping_browser_changed':False}
out=ROOT/'build/webcore-dihos';out.mkdir(exist_ok=True)
(out/'platform-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'sources':len(sources),'dependency_declarations':len(declarations),'webcore_compiled':False}))
