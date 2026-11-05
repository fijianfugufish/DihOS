"""Check the real headless client API using a verified non-PCH CMake command.

The engine archive builds separately. No PCH flags are removed or guessed.
Compilation alone does not link or execute a Page.
"""
from pathlib import Path
import argparse,hashlib,json,platform,shlex,subprocess
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--report',type=Path,default=ROOT/'build/webcore-engine-native/report.json');parser.add_argument('--capture',action='store_true');parser.add_argument('--window',action='store_true');parser.add_argument('--navigation',action='store_true');parser.add_argument('--https',action='store_true');parser.add_argument('--embedded',action='store_true');args=parser.parse_args()
if args.embedded:args.https=True
if args.https:args.navigation=True
if args.navigation:args.window=True
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
engine_path=args.report.resolve();engine=json.loads(engine_path.read_text())
if not engine.get('webcore_configured') or engine['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/configure_native.py'):raise SystemExit('Configure current native engine first')
for field,name in [('webcore_edits_sha256','port-edits.json'),('image_edits_sha256','image-edits.json'),('engine_edits_sha256','engine-edits.json')]:
    if engine[field]!=sha(ROOT/'tools/webcore_dihos'/name):raise SystemExit('Port edits changed; reconfigure')
for relative,digest in engine['platform_files'].items():
    if sha(Path(engine['source'])/relative)!=digest:raise SystemExit('Changed native platform config')
database=Path(engine['build'])/'compile_commands.json'
if sha(database)!=engine['compile_commands_sha256']:raise SystemExit('Changed engine command database')
matches=[entry for entry in json.loads(database.read_text()) if 'Source/WebCore/CMakeFiles/WebCore.dir/' in entry['command'] and entry['file'].endswith('.cpp')]
if not matches:raise SystemExit('Expected a configured native WebCore C++ unit')
entry=matches[0];command=shlex.split(entry['command'])
if '-include-pch' in command:raise SystemExit('Disable WebCore PCH through CMake first')
out=ROOT/'build/webcore-headless-page';out.mkdir(exist_ok=True)
source=ROOT/'tools/webcore_dihos/headless_page_fixture.cpp';obj=out/'headless.o';log=out/'compile.log'
command[command.index('-c')+1]=str(source);command[command.index('-o')+1]=str(obj)
if args.capture:command.insert(1,'-DDIHOS_PAGE_CAPTURE=1')
if args.window:command.insert(1,'-DDIHOS_PAGE_WINDOW=1')
if args.navigation:command.insert(1,'-DDIHOS_PAGE_NAVIGATION=1')
if args.https:command.insert(1,'-DDIHOS_PAGE_HTTPS=1')
if args.embedded:command.insert(1,'-DDIHOS_PAGE_EMBEDDED=1')
with log.open('w') as stream:code=subprocess.call(command,cwd=entry['directory'],stdout=stream,stderr=subprocess.STDOUT,timeout=180)
result={'target':'aarch64-none-elf','builder_sha256':sha(Path(__file__)),'fixture_sha256':sha(source),'strategies_sha256':sha(ROOT/'tools/webcore_dihos/headless_page_strategies.h'),'platform_sha256':sha(ROOT/'tools/webcore_dihos/headless_page_platform.cpp'),'reference_unit':entry['file'],'engine_report_sha256':sha(engine_path),
        'capture':args.capture,'window':args.window,'window_header_sha256':sha(ROOT/'sdk/sacx/include/sacx_user_window.h'),'matching_header_sha256':sha(ROOT/'tools/webcore_dihos/fontconfig/memory.h'),'argv':command,'exit_code':code,'log_sha256':sha(log),'linked':False,'dom_executed':False,'page_rendered':False,'shipping_browser_changed':False}
if not code:
    result['navigation']=args.navigation
    result['https']=args.https
    result['embedded']=args.embedded
    result['navigation_inputs']={str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'tools/webcore_dihos').glob('navigation_*') if p.suffix in ['.h','.cpp']} if args.navigation else {}
    if args.navigation:
        for name in ['sdk/sacx/apps/dihscover/address.h','sdk/sacx/include/sacx_window_keys.h','sdk/sacx/include/sacx_keys.h','sdk/sacx/include/sacx_browser_session.h','tools/webcore_dihos/browser_embedded.cpp','tools/webcore_dihos/error_page.h']:
            result['navigation_inputs'][name]=sha(ROOT/name)
    header=obj.read_bytes()[:20]
    if header[:6]!=b'\x7fELF\x02\x01' or int.from_bytes(header[16:18],'little')!=1 or int.from_bytes(header[18:20],'little')!=183:raise SystemExit('Not a native ARM64 object')
    result['object_sha256']=sha(obj)
    result['support_sources']={};result['support_objects']={}
    sources=[ROOT/'tools/webcore_dihos/headless_page_platform.cpp',
        Path(engine['source'])/'Source/WebCore/platform/text/Hyphenation.cpp',
        Path(engine['source'])/'Source/WebCore/platform/text/LocaleICU.cpp',
        Path(engine['source'])/'Source/WebCore/PAL/pal/text/KillRing.cpp']
    if args.navigation:
        sources.append(ROOT/'tools/webcore_dihos/navigation_input.cpp')
        sources.append(ROOT/'tools/webcore_dihos'/('browser_embedded.cpp' if args.embedded else 'navigation_page.cpp'))
    for support in sources:
        target=out/(support.stem+'.o');args_support=list(command)
        args_support[args_support.index('-c')+1]=str(support)
        args_support[args_support.index('-o')+1]=str(target)
        with (out/(support.stem+'.log')).open('w') as stream:
            code=subprocess.call(args_support,cwd=entry['directory'],stdout=stream,stderr=subprocess.STDOUT,timeout=180)
        result['support_sources'][str(support)]=sha(support)
        if code:break
        result['support_objects'][str(target)]=sha(target)
    result['exit_code']=code
(out/'compile-report.json').write_text(json.dumps(result,indent=2)+'\n')
print('Headless Page client compile:',code,'(not linked/executed)',flush=True)
raise SystemExit(code)
