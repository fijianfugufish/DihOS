"""Configure actual native WebCore targets in an isolated pinned source tree.

Configuration/individual object compilation is not DOM or rendering execution.
Run in ARM64 WSL. Never edits the proven JSC source/build/header caches.
"""
from pathlib import Path
import argparse,hashlib,json,os,platform,re,shlex,shutil,signal,subprocess,tarfile,time
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--timeout',type=int,default=240);parser.add_argument('--graphics',action='store_true');parser.add_argument('--fonts',action='store_true');parser.add_argument('--engine',action='store_true');parser.add_argument('--openssl',action='store_true');args=parser.parse_args()
if args.openssl:args.fonts=True
if args.engine:args.fonts=True
if args.fonts:args.graphics=True
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL; target stays aarch64-none-elf')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
r_path=ROOT/'build/jsc-dihos/report.json';r=json.loads(r_path.read_text())
if not r.get('engine_library_built') or r['target']!='aarch64-none-elf':raise SystemExit('Verified native JSC build required')
for relative,digest in r['sdk_inputs'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('SDK input changed: '+relative)
for group in ['native_vm_sources','native_thread_sources','native_platform_sources','native_random_sources','native_file_sources']:
    for relative,digest in r[group].items():
        if sha(ROOT/relative)!=digest:raise SystemExit('Native adapter changed: '+relative)
archive=ROOT/'build/browser-engine-arm64/wpewebkit-2.54.2.tar.xz'
pin='f3284e19e47fcb5ac8c63c2e1c113dbd65b2423eb8a545f5b345707829004f56'
if sha(archive)!=pin:raise SystemExit('Pinned WebKit archive changed')
edits_path=ROOT/'tools/jsc_dihos/port-edits.json'
if sha(edits_path)!=r['port_edits_sha256']:raise SystemExit('Native JSC edits changed')
old=Path(next(u for u in r['translation_units'] if u['unit']=='allocator')['source']).parents[3]
cache=Path.home()/'.cache/dihos-webcore-native';cache.mkdir(exist_ok=True)
# Bump the recipe when source-tree edits change. Command/report-only changes
# reuse the isolated build's expensive compiler capability checks.
key=hashlib.sha256(('dihos-webcore-source-v3'+sha(edits_path)).encode()).hexdigest()[:12]
if args.graphics:key+='-cairo'
if args.fonts:key+='-fonts'
if args.openssl:key+='-openssl'
source=cache/('source-'+key);build=cache/('build-'+key+('-engine' if args.engine else ''))
out=ROOT/('build/webcore-engine-openssl' if args.openssl and args.engine else 'build/webcore-openssl-native' if args.openssl else 'build/webcore-engine-native' if args.engine else 'build/webcore-fonts-native' if args.fonts else 'build/webcore-cairo' if args.graphics else 'build/webcore-native');out.mkdir(exist_ok=True)
report={'target':'aarch64-none-elf','port':'DihOS','source_version':'2.54.2','archive_sha256':pin,
        'builder_sha256':sha(Path(__file__)),'jsc_report_sha256':sha(r_path),'source':str(source),'build':str(build),
        'webcore_configured':False,'webcore_library_built':False,'dom_executed':False,'page_rendered':False,
        'host_engine_reused':False,'shipping_browser_changed':False,'unified_build':args.engine,'precompiled_headers':args.engine,'webcore_precompiled_headers':False,'bitmap_image_codecs_available':False,'steps':[]}
xml_path=ROOT/('build/webcore-font-xml/report.json' if args.fonts else 'build/webcore-xml/report.json');xml=json.loads(xml_path.read_text())
if not xml.get('library_built') or xml['target']!='aarch64-none-elf' or sha(Path(xml['library']))!=xml['library_sha256']:
    raise SystemExit('Verified target XML library required; run build_xml.py')
report['xml_report_sha256']=sha(xml_path)
if xml['builder_sha256']!=sha(ROOT/('tools/webcore_dihos/build_font_xml.py' if args.fonts else 'tools/webcore_dihos/build_xml.py')) or xml['io_header_sha256']!=sha(ROOT/'tools/webcore_dihos/xml_io_dihos.h'):
    raise SystemExit('XML profile changed; rebuild it')
deps_path=ROOT/'build/webcore-dependencies/report.json';deps=json.loads(deps_path.read_text())
if not deps.get('libraries_built') or deps['target']!='aarch64-none-elf':raise SystemExit('Build native dependencies first')
for library in deps['libraries'].values():
    if sha(Path(library['path']))!=library['sha256']:raise SystemExit('Changed native dependency archive')
report['dependency_report_sha256']=sha(deps_path)
if deps['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_dependencies.py'):raise SystemExit('Dependency builder changed')
for relative,digest in deps['archives'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Dependency archive changed')
graphics=None
if args.graphics:
    graphics_path=ROOT/'build/webcore-graphics/report.json';graphics=json.loads(graphics_path.read_text())
    if not graphics.get('libraries_built') or graphics['target']!='aarch64-none-elf':raise SystemExit('Build verified native graphics first')
    if graphics['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_graphics.py'):raise SystemExit('Graphics recipe changed')
    for path,digest in graphics['libraries'].items():
        if sha(Path(path))!=digest:raise SystemExit('Graphics archive changed')
    for relative,digest in graphics['native_sources'].items():
        if sha(ROOT/relative)!=digest:raise SystemExit('Graphics ABI input changed')
    report['graphics_report_sha256']=sha(graphics_path)

crypto=None
if args.openssl:
    crypto_path=ROOT/'build/webcore-openssl/report.json';crypto=json.loads(crypto_path.read_text())
    validation_path=ROOT/'build/webcore-openssl/validation.json';validation=json.loads(validation_path.read_text())
    if not crypto.get('library_built') or crypto['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_openssl.py'):raise SystemExit('Build current native OpenSSL')
    if not validation.get('crypto_tested') or not validation.get('threading_tested') or validation['native_report_sha256']!=sha(crypto_path) or validation['library_sha256']!=crypto['library_sha256']:raise SystemExit('Validate current native OpenSSL first')
    if sha(Path(crypto['library']))!=crypto['library_sha256']:raise SystemExit('Native crypto archive changed')
    for relative,digest in {**crypto['inputs'],**validation['proof_files']}.items():
        if sha(ROOT/relative)!=digest:raise SystemExit('Native crypto source/evidence changed: '+relative)
    report.update(openssl_report_sha256=sha(crypto_path),openssl_validation_sha256=sha(validation_path),openssl_enabled=True)
font_reports={}
if args.fonts:
    for label,name,builder in [('fonts','webcore-fonts','build_fonts.py'),('matching','webcore-fontconfig','build_fontconfig.py'),('painting','webcore-fontconfig-graphics','build_fontconfig_graphics.py')]:
        path=ROOT/'build'/name/'report.json';entry=json.loads(path.read_text())
        if not (entry.get('library_built') or entry.get('libraries_built')) or entry['builder_sha256']!=sha(ROOT/'tools/webcore_dihos'/builder):raise SystemExit('Rebuild native font dependency: '+name)
        for relative,digest in entry.get('inputs',{}).items():
            if sha(ROOT/relative)!=digest:raise SystemExit('Changed native font ABI')
        archives=dict(entry.get('libraries',{}))
        if 'library' in entry:archives[entry['library']]=entry['library_sha256']
        if 'freetype_library' in entry:archives[entry['freetype_library']]=entry['freetype_library_sha256']
        for library,digest in archives.items():
            if sha(Path(library))!=digest:raise SystemExit('Changed native font library')
        font_reports[label]=entry;report[label+'_report_sha256']=sha(path)
    if font_reports['painting']['fontconfig_report_sha256']!=report['matching_report_sha256']:raise SystemExit('Stale native Cairo Fontconfig dependency')
    if font_reports['matching']['xml_report_sha256']!=sha(xml_path):raise SystemExit('Stale Fontconfig XML ABI')
def save():(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label,command,timeout):
    path=out/(label+'.log');started=time.monotonic()
    with path.open('w') as stream:
        process=subprocess.Popen([str(x) for x in command],cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
        try:code=process.wait(timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid,signal.SIGTERM)
            try:process.wait(5)
            except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
            code=124
    report['steps'].append({'label':label,'argv':[str(x) for x in command],'exit_code':code,
        'seconds':round(time.monotonic()-started,2),'log':str(path),'log_sha256':sha(path)})
    save();print(json.dumps({'step':label,'exit_code':code}),flush=True);return code
if not source.exists():
    # Validate copied source bytes against the pinned archive plus the already
    # tested native JSC edit set. No source marker alone is trusted.
    after={path:entry['after'] for path,entry in r['port_edits'].items()}
    with tarfile.open(archive,'r|xz') as tar:
        for member in tar:
            if not member.isfile():continue
            relative=member.name.removeprefix('wpewebkit-2.54.2/')
            expected=after.get(relative) or hashlib.sha256(tar.extractfile(member).read()).hexdigest()
            if sha(old/relative)!=expected:raise SystemExit('Modified native source: '+relative)
    shutil.copytree(old,source,symlinks=True)
def write_platform():
    options=(source/'Source/cmake/OptionsJSCOnly.cmake').read_text()
    options=options.replace('add_definitions(-DBUILDING_JSCONLY__)','add_definitions(-DBUILDING_DIHOS_WEBKIT__=1 -DWTF_PLATFORM_DIHOS=1)')
    options=options.replace('set(ENABLE_WEBCORE OFF)','set(ENABLE_WEBCORE ON)')
    options=options.replace('set(ENABLE_API_TESTS ON)','set(ENABLE_API_TESTS OFF)')
    features=(source/'Source/cmake/WebKitFeatures.cmake').read_text()
    known=set(re.findall(r'WEBKIT_OPTION_DEFINE\((\w+)',features))
    proposed=['ENABLE_VIDEO','ENABLE_WEB_AUDIO','ENABLE_MEDIA_SOURCE','ENABLE_MEDIA_STREAM','ENABLE_WEB_RTC',
        'ENABLE_WEBGL','ENABLE_WEBGPU','ENABLE_ACCESSIBILITY','ENABLE_SPELLCHECK','ENABLE_GEOLOCATION',
        'ENABLE_NOTIFICATIONS','ENABLE_GAMEPAD','ENABLE_XSLT','ENABLE_WEB_CRYPTO','ENABLE_ENCRYPTED_MEDIA',
        'ENABLE_INDEXED_DATABASE','ENABLE_SERVICE_WORKER','ENABLE_OFFSCREEN_CANVAS',
        'USE_LCMS','USE_WOFF2','USE_JPEGXL','USE_AVIF']
    disabled=[name for name in proposed if name in known]
    overrides='\n'.join(f'WEBKIT_OPTION_DEFAULT_PORT_VALUE({name} PRIVATE OFF)' for name in disabled)+'\n'
    options=options.replace('WEBKIT_OPTION_END()',overrides+'WEBKIT_OPTION_END()',1)
    options+='\nset(ENABLE_TOOLS OFF)\nset(WebCore_LIBRARY_TYPE STATIC)\nset(PAL_LIBRARY_TYPE STATIC)\n'
    options+='find_package(LibXml2 REQUIRED)\nfind_package(SQLite3 REQUIRED)\nfind_package(ZLIB REQUIRED)\n'
    if args.graphics:
        options+='SET_AND_EXPOSE_TO_BUILD(USE_CAIRO TRUE)\nSET_AND_EXPOSE_TO_BUILD(ENABLE_ACCESSIBILITY FALSE)\nfind_package(Cairo REQUIRED)\n'
    if args.fonts:
        options+='SET_AND_EXPOSE_TO_BUILD(USE_FREETYPE TRUE)\nSET_AND_EXPOSE_TO_BUILD(USE_HARFBUZZ TRUE)\nSET_AND_EXPOSE_TO_BUILD(USE_FONTCONFIG TRUE)\n'
        specs=[('Freetype::Freetype',font_reports['matching']['freetype_library'],font_reports['fonts']['includes'],''),
               ('Fontconfig::Fontconfig',font_reports['matching']['library'],[font_reports['matching']['stage']+'/include'],'Freetype::Freetype;LibXml2::LibXml2'),
               ('HarfBuzz::HarfBuzz',next(p for p in font_reports['fonts']['libraries'] if p.endswith('/libharfbuzz.a')),font_reports['fonts']['includes'],'Freetype::Freetype'),
               ('HarfBuzz::ICU',next(p for p in font_reports['fonts']['libraries'] if p.endswith('/libharfbuzz-icu.a')),font_reports['fonts']['includes'],'HarfBuzz::HarfBuzz;ICU::uc')]
        for name,library,includes,links in specs:
            options+=f'add_library({name} STATIC IMPORTED GLOBAL)\nset_target_properties({name} PROPERTIES IMPORTED_LOCATION "{library}" INTERFACE_INCLUDE_DIRECTORIES "{";".join(includes)}" INTERFACE_LINK_LIBRARIES "{links}")\n'
    # CMake's built-in module exports SQLite::SQLite3; WebCore uses the
    # historical SQLite3 namespace. This aliases the actual native library.
    if args.openssl:
        includes=[*crypto['includes'],str(ROOT/'tools/webcore_dihos/openssl')]
        options+='SET_AND_EXPOSE_TO_BUILD(USE_OPENSSL TRUE)\n'
        options+=f'add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)\nset_target_properties(OpenSSL::Crypto PROPERTIES IMPORTED_LOCATION "{crypto["library"]}" INTERFACE_INCLUDE_DIRECTORIES "{";".join(includes)}" INTERFACE_COMPILE_DEFINITIONS "OPENSSL_SYS_DIHOS=1")\n'
    options+='if (TARGET SQLite::SQLite3 AND NOT TARGET SQLite3::SQLite3)\n    add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)\nendif ()\n'
    (source/'Source/cmake/OptionsDihOS.cmake').write_text(options)
    common=source/'Source/cmake/WebKitCommon.cmake';text=(old/'Source/cmake/WebKitCommon.cmake').read_text()
    if text.count('set(ALL_PORTS\n')!=1:raise SystemExit('Unsupported port list changed')
    common.write_text(text.replace('set(ALL_PORTS\n','set(ALL_PORTS\n        DihOS\n',1))
    for directory in ['Source/WTF/wtf','Source/JavaScriptCore']:
        (source/directory/'PlatformDihOS.cmake').write_text('include("${CMAKE_CURRENT_LIST_DIR}/PlatformJSCOnly.cmake")\n')
    # Initially use common WebCore/PAL source sets; backend obligations will be
    # exposed by real compile/link failures, not success-returning substitutes.
    platform_text=(
        '# Core bring-up has no GL backend; software graphics is still to be implemented.\n'
        'if (ENABLE_WEBGL OR USE_ANGLE_EGL OR USE_LIBEPOXY)\n'
        '    message(FATAL_ERROR "DihOS has no native GL backend")\nendif ()\n'
        'list(REMOVE_ITEM WebCore_LIBRARIES OpenGL::GLES)\n'
        'list(APPEND WebCore_PRIVATE_FRAMEWORK_HEADERS platform/dihos/ResourceError.h platform/dihos/ResourceRequest.h platform/dihos/ResourceResponse.h platform/dihos/CertificateInfo.h platform/dihos/AuthenticationChallenge.h)\n'
        'list(APPEND WebCore_SOURCES platform/dihos/ResourceResponseDihOS.cpp platform/dihos/WebCoreResourceLoad.cpp platform/dihos/ResourceHandleDihOS.cpp platform/dihos/SharedMemoryDihOS.cpp platform/video-codecs/BitReader.cpp)\n'
        f'list(APPEND WebCore_SOURCES "{ROOT}/tools/webcore_dihos/ResourceLoad.cpp" "{ROOT}/tools/webcore_dihos/DispatchQueue.cpp")\n'
        f'list(APPEND WebCore_PRIVATE_INCLUDE_DIRECTORIES "${{WEBCORE_DIR}}/platform/dihos" "${{WEBCORE_DIR}}/platform/dihos/sdk" "{ROOT}/tools/webcore_dihos")\n')
    # WebCore's optional prefix headers import POSIX pthread/setjmp headers.
    # Its supported header-independent build uses the native SACX adapters;
    # retain unified sources and sibling JSC/WTF PCH without inventing POSIX APIs.
    # WebCore is declared before this platform file. Initialize future
    # subtargets and also update the already-declared main target explicitly.
    platform_text+='set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)\nset_target_properties(WebCore PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)\n'
    if args.graphics:
        platform_text+='include(platform/Cairo.cmake)\n'
        platform_text+=f'list(APPEND WebCore_LIBRARIES "{graphics["stage"]}/lib/libpixman-1.a")\n'
    if args.fonts:platform_text+='include(platform/FreeType.cmake)\n'
    if args.openssl:
        platform_text+='include(platform/OpenSSL.cmake)\nlist(APPEND WebCore_LIBRARIES OpenSSL::Crypto)\n'
        (source/'Source/WebCore/PAL/pal/PlatformDihOS.cmake').write_text('list(APPEND PAL_SOURCES crypto/openssl/CryptoDigestOpenSSL.cpp)\nlist(APPEND PAL_LIBRARIES OpenSSL::Crypto)\n')
    (source/'Source/WebCore/PlatformDihOS.cmake').write_text(platform_text)
    platform_dir=source/'Source/WebCore/platform/dihos';platform_dir.mkdir(exist_ok=True)
    for name in ['ResourceError.h','ResourceRequest.h','ResourceResponse.h','CertificateInfo.h','AuthenticationChallenge.h','ByteOrderDihOS.h','DNSResolveQueueDihOS.h','ResourceResponseDihOS.cpp','WebCoreResourceLoad.h','WebCoreResourceLoad.cpp','NativeHandleState.h','ResourceHandleDihOS.cpp','SharedMemoryDihOS.cpp','PlatformImageDihOS.h']:
        data=(ROOT/'tools/webcore_dihos/platform'/name).read_bytes()
        target=platform_dir/name
        if not target.exists() or target.read_bytes()!=data:target.write_bytes(data)
    (platform_dir/'sdk').mkdir(exist_ok=True)
    (platform_dir/'sdk/sacx_user_network.h').write_bytes((ROOT/'sdk/sacx/include/sacx_user_network.h').read_bytes())
    (platform_dir/'sdk/sacx_user_http_request.h').write_bytes((ROOT/'sdk/sacx/include/sacx_user_http_request.h').read_bytes())
    (platform_dir/'sdk/http_request_headers.h').write_bytes((ROOT/'kernel/include/net/http_request_headers.h').read_bytes())
    (source/'.dihos-native-port.json').write_text(json.dumps({'disabled_features':disabled},indent=2))
write_platform()
tree_edits=ROOT/'tools/webcore_dihos/port-edits.json'
image_edits=ROOT/'tools/webcore_dihos/image-edits.json'
engine_edits=ROOT/'tools/webcore_dihos/engine-edits.json'
edits={**json.loads(tree_edits.read_text()),**json.loads(image_edits.read_text()),**json.loads(engine_edits.read_text())}
for relative,edit in edits.items():
    original=(old/relative).read_bytes()
    if hashlib.sha256(original).hexdigest()!=edit['before_sha256']:raise SystemExit('Unexpected WebCore patch base')
    text=original.decode()
    for operation in edit.get('operations',[edit]):
        if text.count(operation['match'])!=operation.get('matches',1):raise SystemExit('Ambiguous WebCore patch')
        replacement=operation['replacement']
        if args.graphics and relative.endswith('/PlatformImage.h'):
            replacement=replacement.replace('#if PLATFORM(DIHOS)','#if PLATFORM(DIHOS) && !USE(CAIRO)')
        text=text.replace(operation['match'],replacement)
    updated=text.encode()
    target=source/relative
    if target.read_bytes()!=updated:target.write_bytes(updated)
report['webcore_edits_sha256']=sha(tree_edits)
report['image_edits_sha256']=sha(image_edits)
report['engine_edits_sha256']=sha(engine_edits)
report['platform_files']={str(p.relative_to(source)):sha(p) for p in [
    source/'Source/cmake/OptionsDihOS.cmake',source/'Source/cmake/WebKitCommon.cmake',
    source/'Source/WebCore/PlatformDihOS.cmake',source/'Source/WTF/wtf/PlatformDihOS.cmake',
    source/'Source/JavaScriptCore/PlatformDihOS.cmake',source/'Source/WTF/wtf/RedBlackTree.h']}
if args.openssl:
    relative='Source/WebCore/PAL/pal/PlatformDihOS.cmake';report['platform_files'][relative]=sha(source/relative)
if args.graphics:
    # Reuse upstream's reference-counted generic wrapper ownership. This adds
    # no accessibility platform service or successful notification stubs.
    relative='Source/WebCore/accessibility/AXCoreObject.h'
    text=(old/relative).read_text();marker='#elif PLATFORM(PLAYSTATION) || PLATFORM(HAIKU)'
    if text.count(marker)!=2:raise SystemExit('WebCore generic AX wrapper contract changed')
    text=text.replace(marker,marker+' || PLATFORM(DIHOS)')
    target=source/relative
    if target.read_text()!=text:target.write_text(text)
    report['platform_files'][relative]=sha(target)
    relative='Source/WebCore/accessibility/AXObjectCache.h'
    text=(old/relative).read_text()
    if text.count('getpid()')!=1:raise SystemExit('WebCore AX diagnostic identity changed')
    text=text.replace('getpid()','getCurrentProcessID()')
    target=source/relative
    if target.read_text()!=text:target.write_text(text)
    report['platform_files'][relative]=sha(target)
report['native_platform_sources']={str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'tools/webcore_dihos/platform').iterdir() if p.suffix in ['.h','.cpp']}
for relative in ['sdk/sacx/include/sacx_user_network.h','sdk/sacx/include/sacx_user_http_request.h','kernel/include/net/http_request_headers.h','tools/webcore_dihos/ResourceLoad.h','tools/webcore_dihos/ResourceLoad.cpp','tools/webcore_dihos/DispatchQueue.h','tools/webcore_dihos/DispatchQueue.cpp']:
    report['native_platform_sources'][relative]=sha(ROOT/relative)
report.update(json.loads((source/'.dihos-native-port.json').read_text()))
base_toolchain=Path(next(s for s in r['steps'] if s['label']=='engine-configure')['argv'][
    next(i for i,a in enumerate(next(s for s in r['steps'] if s['label']=='engine-configure')['argv']) if str(a).startswith('-DCMAKE_TOOLCHAIN_FILE='))].split('=',1)[1])
report['toolchain_sha256']=sha(base_toolchain)
command=['cmake','-S',source,'-B',build,'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(base_toolchain),
    '-DDIHOS_SDK_ROOT='+str(ROOT),'-DDIHOS_RUNTIME_ARCHIVE='+r['runtime_archive']['path'],
    '-DPORT=DihOS','-DCMAKE_BUILD_TYPE=Release','-DENABLE_STATIC_JSC=ON','-DUSE_HEADER_MAPS=OFF',
    '-DENABLE_JIT=OFF','-DENABLE_DFG_JIT=OFF','-DENABLE_FTL_JIT=OFF','-DENABLE_C_LOOP=ON',
    '-DENABLE_WEBASSEMBLY=OFF','-DENABLE_SAMPLING_PROFILER=OFF','-DENABLE_REMOTE_INSPECTOR=OFF',
    '-DUSE_LIBBACKTRACE=OFF','-DUSE_SYSTEM_MALLOC=ON','-DENABLE_UNIFIED_BUILDS='+('ON' if args.engine else 'OFF'),
    '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DCMAKE_DISABLE_PRECOMPILE_HEADERS='+('OFF' if args.engine else 'ON'),
    '-DICU_ROOT='+str(Path(r['icu']['sysroot'])/'usr')]
command += ['-DLIBXML2_LIBRARY='+xml['library'],'-DLIBXML2_INCLUDE_DIR='+str(Path(xml['stage'])/'include/libxml2')]
if args.graphics:
    cairo_stage=font_reports['painting']['stage'] if args.fonts else graphics['stage']
    command+=['-DCairo_LIBRARY='+str(Path(cairo_stage)/'lib/libcairo.a'),'-DCairo_INCLUDE_DIR='+str(Path(cairo_stage)/'include/cairo')]
command += ['-D'+feature+'=OFF' for feature in report['disabled_features']]
command += ['-DSQLite3_LIBRARY='+deps['libraries']['sqlite3']['path'],
            '-DSQLite3_INCLUDE_DIR='+deps['libraries']['sqlite3']['include'],
            '-DZLIB_LIBRARY='+deps['libraries']['z']['path'],'-DZLIB_INCLUDE_DIR='+deps['libraries']['z']['include']]
# Preserve the proven native feature/thread dependency test, including actual
# linkable runtime support, when configuring a separate product port.
original=next(s for s in r['steps'] if s['label']=='engine-configure')['argv']
command += [a for a in original if str(a).startswith(('-DDIHOS_THREAD_SMOKE=','-DCMAKE_EXE_LINKER_FLAGS='))]
code=run('configure',command,args.timeout)
report['webcore_configured']=code==0
if not code:
    targets=subprocess.check_output(['ninja','-C',str(build),'-t','targets','all'],text=True)
    (out/'targets.txt').write_text(targets)
    report['webcore_target_present']=any(line.startswith('WebCore:') or line.startswith('lib/libWebCore.a:') for line in targets.splitlines())
    report['compile_commands_sha256']=sha(build/'compile_commands.json')
save();print(json.dumps({'report':str(out/'report.json'),'webcore_configured':not code,'dom_executed':False}),flush=True)
raise SystemExit(code)
