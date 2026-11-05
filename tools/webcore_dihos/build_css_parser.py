"""Relink verified engine archives with explicit native networking shim updates.

The previous engine/source reports and archives are read-only. The DihOS decoder factory translation unit is rebuilt in full to avoid stale
inlined null-decoder calls; the baseline archives remain read-only.
"""
from pathlib import Path
import hashlib,json,shlex,shutil,subprocess,struct
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/dihscover-css-parser-validation/renderer';OUT.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
base_path=ROOT/'build/webcore-headless-page/report.json';base=json.loads(base_path.read_text())
client_path=ROOT/'build/webcore-headless-page/compile-report.json';client=json.loads(client_path.read_text())
config_path=ROOT/'build/webcore-engine-openssl/report.json';config=json.loads(config_path.read_text())
assert base['linked'] and base['loader_accepted'] and base['embedded']
assert base['engine_report_sha256']==sha(config_path) and base['client_compile_report_sha256']==sha(client_path)
for name,digest in base['linked_inputs'].items():assert sha(Path(name))==digest,name
for name,digest in config['platform_files'].items():assert sha(Path(config['source'])/name)==digest,name
overlay=OUT/'include/sdk';overlay.mkdir(parents=True,exist_ok=True)
sdk_cache=Path(next(a[2:] for a in client['argv'] if a.startswith('-I') and '/headers-' in a and a.endswith('/sdk')))
platform_cache=Path(config['source'])/'Source/WebCore/platform/dihos'
# Link header aliases instead of duplicate copies: pragma-once headers must
# have one filesystem identity when included through both sdk/ and libc++.
for p in (ROOT/'sdk/sacx/include').glob('*.h'):
    target=overlay/p.name
    if target.exists():target.unlink()
    cached=sdk_cache/p.name
    if not cached.exists():cached=platform_cache/'sdk'/p.name
    selected=p if p.name in ['sacx_user_network.h','sacx_http_limits.h','sacx_user_http_request.h'] or not cached.exists() else cached
    if selected!=p:assert sha(selected)==sha(p),p.name
    target.symlink_to(selected)
target=overlay/'http_request_headers.h'
if target.exists():target.unlink()
target.symlink_to(ROOT/'kernel/include/net/http_request_headers.h')
staged=OUT/'platform';staged.mkdir(exist_ok=True)
for p in (ROOT/'tools/webcore_dihos/platform').glob('*.h'):
    target=staged/p.name
    if target.exists():target.unlink()
    original=Path(config['source'])/'Source/WebCore/platform/dihos'/p.name
    target.symlink_to(p if p.name in ['WebCoreResourceLoad.h','ResponseHeadersDihOS.h','DihOSImageDecoder.h'] or not original.exists() else original)
frame_headers=ROOT/'build/dihscover-complex-sites-validation/renderer/headers'
subprocess.run(['python3',str(ROOT/'tools/webcore_dihos/prepare_complex_headers.py')],check=True)
if not (frame_headers/'WebCore/EmptyFrameLoaderClient.h').is_file():
    subprocess.run(['python3',str(ROOT/'tools/webcore_dihos/prepare_cookie_headers.py')],cwd=ROOT,check=True)
assert (frame_headers/'WebCore/EmptyFrameLoaderClient.h').is_file()
includes=['-I'+str(frame_headers),'-I'+str(frame_headers/'WebCore'),'-I'+str(OUT/'include'),'-I'+str(overlay),'-I'+str(staged)]
report={'baseline_report_sha256':sha(base_path),'baseline_compile_sha256':sha(client_path),'baseline_engine_sha256':sha(config_path),'sources':{},'objects':{},'steps':[],'linked':False,'loader_accepted':False,'frame_header_sha256':sha(frame_headers/'WebCore/EmptyFrameLoaderClient.h')}
def save():(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(name,args,cwd=ROOT):
    with (OUT/(name+'.log')).open('w') as f:result=subprocess.run(list(map(str,args)),cwd=cwd,stdout=f,stderr=subprocess.STDOUT)
    report['steps'].append({'label':name,'argv':list(map(str,args)),'exit_code':result.returncode});save()
    if result.returncode:raise SystemExit(name+' failed; inspect '+str(OUT/(name+'.log')))
def compile_source(source,command,name,cwd=ROOT):
    target=OUT/(name+'.o');command=list(command)
    actual=source
    if source.parent==ROOT/'tools/webcore_dihos/platform':
        actual=staged/source.name;shutil.copy2(source,actual)
    command[command.index('-c')+1]=str(actual);command[command.index('-o')+1]=str(target)
    command[1:1]=includes
    run('compile-'+name,command,cwd)
    report['sources'][str(source)]=sha(source);report['objects'][str(target)]=sha(target);return target
template=client['argv'];replacements={}
replacements[str(ROOT/'build/webcore-headless-page/headless.o')]=str(compile_source(ROOT/'tools/webcore_dihos/headless_page_fixture.cpp',template,'headless'))
for source in client['support_sources']:
    path=Path(source);target=compile_source(path,template,path.stem)
    replacements[str(ROOT/'build/webcore-headless-page'/(path.stem+'.o'))]=str(target)
database=json.loads((Path(config['build'])/'compile_commands.json').read_text())
overrides=[]
for name,source in [('WebCoreResourceLoad',ROOT/'tools/webcore_dihos/platform/WebCoreResourceLoad.cpp'),('ResourceHandleDihOS',ROOT/'tools/webcore_dihos/platform/ResourceHandleDihOS.cpp'),('ResourceLoad',ROOT/'tools/webcore_dihos/ResourceLoad.cpp')]:
    entry=next(e for e in database if e['file'].endswith('/'+name+'.cpp'))
    overrides.append(str(compile_source(source,shlex.split(entry['command']),name,entry['directory'])))
# Rebuild the complete factory unit, including any unified-source siblings.
factory=Path(config['source'])/'Source/WebCore/platform/graphics/ImageDecoder.cpp'
factory_text=factory.read_text()
needle='    // No native bitmap codecs yet: reject decoding, never fabricate an image.\n    UNUSED_PARAM(data);\n    UNUSED_PARAM(alphaOption);\n    UNUSED_PARAM(gammaAndColorProfileOption);\n    return nullptr;'
assert factory_text.count(needle)==1
factory_text=factory_text.replace('#include "ImageDecoder.h"','#include "ImageDecoder.h"\n#include "DihOSImageDecoder.h"',1)
factory_text=factory_text.replace(needle,'    return createDihOSImageDecoder(data, mimeType, alphaOption, gammaAndColorProfileOption);',1)
factory_text=factory_text.replace('#elif !PLATFORM(DIHOS)','#elif PLATFORM(DIHOS)\n    return type == MediaType::Image;\n#else',1)
patched=staged/'ImageDecoderFactory.cpp';patched.write_text(factory_text)
entry=next((e for e in database if Path(e['file'])==factory),None)
if entry:
    factory_command=shlex.split(entry['command'])
    unit=patched
else:
    entry=next(e for e in database if 'unified-sources' in e['file'] and Path(e['file']).is_file() and 'platform/graphics/ImageDecoder.cpp' in Path(e['file']).read_text())
    import re
    original_unit=Path(entry['file']);unit=staged/'ImageDecoderUnified.cpp'
    source=original_unit.read_text()
    source,count=re.subn(r'#include\s+"[^"]*platform/graphics/ImageDecoder\.cpp"',lambda _: '#include "'+str(patched)+'"',source)
    assert count==1
    unit.write_text(source);factory_command=shlex.split(entry['command'])
    report['sources'][str(original_unit)]=sha(original_unit)
report['decoder_factory']={'original_sha256':sha(factory),'patched_sha256':sha(patched),'unit_sha256':sha(unit)}
overrides.append(str(compile_source(unit,factory_command,'ImageDecoderFactory',entry['directory'])))
decoder_command=list(template);decoder_command[1:1]=['-I'+str(ROOT/'kernel/include'),'-I'+str(ROOT/'third_party/simplewebp')]
overrides.append(str(compile_source(ROOT/'tools/webcore_dihos/platform/DihOSImageDecoder.cpp',decoder_command,'DihOSImageDecoder')))
report['sources'][str(ROOT/'kernel/include/third_party/stb_image.h')]=sha(ROOT/'kernel/include/third_party/stb_image.h')
report['sources'][str(ROOT/'third_party/simplewebp/simplewebp.h')]=sha(ROOT/'third_party/simplewebp/simplewebp.h')
# Rebuild the exact CSS parser unit and the private allocator; engine archives stay read-only.
from css_parser_fallback import patch_css_parser
parser=Path(config["source"])/"Source/WebCore/css/parser/CSSParser.cpp"
patched_parser=staged/"CSSParserDihOS.cpp";patched_parser.write_text(patch_css_parser(parser.read_text()))
entry=next((e for e in database if Path(e["file"])==parser),None)
if entry:
    parser_command=shlex.split(entry["command"]);unit=patched_parser
else:
    entry=next(e for e in database if "unified-sources" in e["file"] and Path(e["file"]).is_file() and "css/parser/CSSParser.cpp" in Path(e["file"]).read_text())
    import re
    unified=Path(entry["file"]).read_text()
    def include(match):
        name=match.group(1)
        if name=="css/parser/CSSParser.cpp":return '#include "'+str(patched_parser)+'"'
        return '#include "'+str(Path(config["source"])/"Source/WebCore"/name)+'"'
    unit=staged/"CSSParserUnified.cpp";unit.write_text(re.sub(r'#include "([^"]+)"',include,unified));parser_command=shlex.split(entry["command"])
parser_command[parser_command.index("-c")+1]=str(unit);parser_command[parser_command.index("-o")+1]=str(OUT/"CSSParserDihOS.o");parser_command[1:1]=includes
run("compile-CSSParserDihOS",parser_command,entry["directory"])
report["objects"][str(OUT/"CSSParserDihOS.o")]=sha(OUT/"CSSParserDihOS.o")
report["css_parser_original_sha256"]=sha(parser);report["css_parser_patched_sha256"]=sha(patched_parser)
report["sources"][str(ROOT/"tools/webcore_dihos/css_parser_fallback.py")]=sha(ROOT/"tools/webcore_dihos/css_parser_fallback.py")
overrides.append(str(OUT/"CSSParserDihOS.o"))
allocator_command=list(template);allocator_command[1:1]=["-I"+str(ROOT/"sdk/sacx/user_runtime/include"),"-I"+str(ROOT/"third_party/tlsf")]
overrides.append(str(compile_source(ROOT/"sdk/sacx/user_runtime/allocator.cpp",allocator_command,"allocator")))
cookie_command=list(template);cookie_command[1:1]=['-I'+str(ROOT/'tools/webcore_dihos'),'-I'+str(OUT.parent)]
for name,path in [('BlobRegistryDihOS',ROOT/'tools/webcore_dihos/platform/BlobRegistryDihOS.cpp'),('SessionCookiesDihOS',ROOT/'tools/webcore_dihos/platform/SessionCookiesDihOS.cpp'),('session_psl',ROOT/'tools/webcore_dihos/session_psl.cpp')]:
    overrides.append(str(compile_source(path,cookie_command,name)))
report['psl_sha256']=sha(OUT.parent/'public_suffix_list.dat')
report['sources'][str(ROOT/'tools/webcore_dihos/session_cookies.h')]=sha(ROOT/'tools/webcore_dihos/session_cookies.h')
report['sources'][str(OUT.parent/'browser_psl.inc')]=sha(OUT.parent/'browser_psl.inc')
link=list(next(s['argv'] for s in base['steps'] if s['label']=='link'))
link=[replacements.get(a,a) for a in link];link[link.index('-o')+1]=str(OUT/'page.elf')
link[link.index('--start-group'):link.index('--start-group')]=overrides
report['linked_inputs']={name:sha(Path(name)) for name in link if name.endswith(('.o','.a','.ld'))}
run('link',link)
elf=OUT/'page.elf';symbols=OUT/'page.symbols.elf';shutil.copy2(elf,symbols)
def segments(p):
    data=p.read_bytes();at=struct.unpack_from('<Q',data,32)[0];count=struct.unpack_from('<H',data,56)[0];result=[]
    for i in range(count):
        kind,flags,off,addr,physical,size,memory,alignment=struct.unpack_from('<IIQQQQQQ',data,at+i*56)
        if kind==1:result.append((flags,addr,size,memory,alignment,hashlib.sha256(data[off:off+size]).hexdigest()))
    return result
before=segments(elf);strip=shutil.which('llvm-strip-22') or shutil.which('aarch64-linux-gnu-strip');assert strip
run('strip',[strip,'--strip-all',elf]);assert segments(elf)==before
run('inspector-build',['clang-22','-Ikernel/include','tools/inspect_user_elf.c','kernel/process/dihos_user_elf.c','-o',OUT/'inspect'])
run('loader-check',[OUT/'inspect',elf])
run('imports',['aarch64-linux-gnu-nm','-u',symbols]);assert not (OUT/'imports.log').read_text().strip()
for directory in [ROOT/'tools/webcore_dihos/platform',ROOT/'sdk/sacx/include']:
    for p in directory.glob('*.h'):report['sources'][str(p)]=sha(p)
report.update(linked=True,loader_accepted=True,embedded=True,elf_sha256=sha(elf),elf_bytes=elf.stat().st_size,symbols_sha256=sha(symbols),upstream_archives_unchanged=True)
fixture=compile_source(ROOT/'tools/webcore_dihos/webcore_resource_fixture.cpp',template,'resource-fixture')
fixture_link=[str(fixture) if a==str(OUT/'headless.o') else a for a in link]
fixture_link=[a for a in fixture_link if a not in [str(OUT/'browser_embedded.o'),str(OUT/'navigation_input.o')]]
fixture_link[fixture_link.index('-o')+1]=str(OUT/'resource.elf')
run('fixture-link',fixture_link)
run('fixture-loader',[OUT/'inspect',OUT/'resource.elf'])
report.update(fixture_elf_sha256=sha(OUT/'resource.elf'))
image_headless=compile_source(ROOT/'tools/webcore_dihos/headless_page_fixture.cpp',[*template,'-DDIHOS_IMAGE_TEST_FIXTURE=1'],'image-headless')
image_link=[str(image_headless) if a==str(OUT/'headless.o') else a for a in link]
image_link[image_link.index('-o')+1]=str(OUT/'images-test.elf')
run('images-test-link',image_link)
run('images-test-loader',[OUT/'inspect',OUT/'images-test.elf'])
report.update(image_test_elf_sha256=sha(OUT/'images-test.elf'))
save();print('PASS: updated native loading shims, embedded ARM64 renderer and resource fixture linked; production loader accepted; execution still requires tests')


