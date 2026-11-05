"""Recompile the proven browser shims against the video-enabled engine ABI.

The older CSS/image/network fixes are retained, but no WebCore-layout object or
archive from the disabled-video build is reused. Supports compile-only while
the isolated engine finishes building.
"""
from pathlib import Path
import argparse,hashlib,json,re,shlex,shutil,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/webcore_dihos'))
from css_parser_fallback import patch_css_token_budget
from indexeddb_provider import unavailable_delegate
from software_view_transitions import patch_document
p=argparse.ArgumentParser();p.add_argument('--compile-only',action='store_true');p.add_argument('--fixture-only',action='store_true');p.add_argument('--resume',action='store_true');p.add_argument('--mse',action='store_true');p.add_argument('--software-view-transitions',action='store_true');p.add_argument('--engine-report',type=Path);p.add_argument('--output',type=Path);args=p.parse_args()
OUT=(ROOT/args.output/'renderer').resolve() if args.output else ROOT/('build/dihscover-mse-player/renderer' if args.mse else 'build/dihscover-video-player/renderer')
assert OUT.is_relative_to(ROOT/'build');OUT.mkdir(parents=True,exist_ok=True)
OLD=ROOT/'build/dihscover-css-parser-validation/renderer'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
old=json.loads((OLD/'report.json').read_text())
oldconfig=json.loads((ROOT/'build/webcore-engine-openssl/report.json').read_text())
engine_path=ROOT/('build/dihscover-mse-player-engine/report.json' if args.mse else 'build/dihscover-video-engine/report.json');engine=json.loads(engine_path.read_text())
if args.engine_report:engine_path=args.engine_report.resolve();engine=json.loads(engine_path.read_text())
assert engine['video_enabled'] and (engine.get('engine_configured') or (args.mse and engine.get('config_sha256')))
build,source=Path(engine['build']),Path(engine['source'])
assert engine['config_sha256']==sha(build/'cmakeconfig.h')
for name,digest in engine['platform_files'].items():assert sha(source/name)==digest,name
for name,digest in old['linked_inputs'].items():assert sha(Path(name))==digest,name
maps=[(str(OLD),str(OUT)),(oldconfig['source'],str(source)),(oldconfig['build'],str(build)),
    (str(ROOT/'build/dihscover-complex-sites-validation/renderer/headers'),str(OUT/'headers'))]
def remap(value):
    for before,after in maps:value=value.replace(before,after)
    return value
for folder in ['include/sdk','platform']:
    (OUT/folder).mkdir(parents=True,exist_ok=True)
    for item in (OLD/folder).iterdir():
        if not item.is_file():continue
        dest=OUT/folder/item.name
        if item.is_symlink():
            target=Path(remap(str(item.resolve())))
            if dest.is_symlink() and dest.readlink()==target:continue
            assert not dest.exists(),dest
            dest.symlink_to(target)
        else:
            data=remap(item.read_text())
            if folder=='platform' and item.name=='CSSParserDihOS.cpp':data=patch_css_token_budget(data)
            if folder=='platform' and item.name in ['WebCoreResourceLoad.cpp','ResourceHandleDihOS.cpp']:
                data=remap((ROOT/'tools/webcore_dihos/platform'/item.name).read_text())
            if not dest.exists() or dest.read_text()!=data:dest.write_text(data)
for name in ['DatabaseProviderDihOS.h','DatabaseProviderDihOS.cpp']:
    data=(ROOT/'tools/webcore_dihos/platform'/name).read_text()
    dest=OUT/'platform'/name
    if not dest.exists() or dest.read_text()!=data:dest.write_text(data)
delegate=unavailable_delegate((source/'Source/WebCore/loader/EmptyClients.cpp').read_text())
dest=OUT/'platform/UnavailableIDBDelegate.inc'
if not dest.exists() or dest.read_text()!=delegate:dest.write_text(delegate)
diagnostics=ROOT/'tools/webcore_dihos/browser_diagnostics.h'
target=OUT/'platform/browser_diagnostics.h'
if not target.exists() or target.read_text()!=diagnostics.read_text():target.write_text(diagnostics.read_text())
# The only private client overrides are final->override; no member layouts
# change. Every other header aliases this engine's forwarding-header view.
headers=OUT/'headers/WebCore';headers.mkdir(parents=True,exist_ok=True)
edits={
    'EmptyClients.h':('void addMessageToConsole(JSC::MessageSource, JSC::MessageLevel, const String&, unsigned, unsigned, const String&) final { }',
                      'void addMessageToConsole(JSC::MessageSource, JSC::MessageLevel, const String&, unsigned, unsigned, const String&) override { }'),
    'EmptyFrameLoaderClient.h':('RefPtr<LocalFrame> createFrame(const AtomString&, HTMLFrameOwnerElement&) final;',
                               'RefPtr<LocalFrame> createFrame(const AtomString&, HTMLFrameOwnerElement&) override;')}
for item in (build/'WebCore/PrivateHeaders/WebCore').iterdir():
    dest=headers/item.name
    if item.name in edits:
        text=item.read_text();before,after=edits[item.name];assert text.count(before)==1
        text=text.replace(before,after)
        if not dest.exists() or dest.read_text()!=text:dest.write_text(text)
    elif not dest.exists():dest.symlink_to(item,target_is_directory=item.is_dir())
    else:assert dest.is_symlink() and dest.readlink()==item,dest
for name in ['browser_psl.inc','public_suffix_list.dat']:
    original=OLD.parent/name;target=OUT.parent/name
    if not target.exists():shutil.copy2(original,target)
    assert sha(original)==sha(target)
report={'engine_report_sha256':sha(engine_path),'builder_sha256':sha(Path(__file__)),
        'baseline_report_sha256':sha(OLD/'report.json'),'objects':{},'sources':{},
        'steps':[],'linked':False,'browser_video_tested':False}
def save():(OUT/('fixture-compile-report.json' if args.fixture_only else 'report.json')).write_text(json.dumps(report,indent=2)+'\n')
def run(label,argv,cwd=ROOT):
    with (OUT/(label+'.log')).open('w') as log:
        code=subprocess.call(list(map(str,argv)),cwd=cwd,stdout=log,stderr=subprocess.STDOUT)
    report['steps'].append({'label':label,'argv':list(map(str,argv)),'exit_code':code});save()
    print(label,code,flush=True)
    if code:raise SystemExit('Inspect '+str(OUT/(label+'.log')))
compiler_hashes={}
def compile_object(label,command):
    src=Path(command[command.index('-c')+1]);obj=Path(command[command.index('-o')+1])
    dep=obj.with_suffix('.d');stamp=obj.with_suffix('.compile.json')
    command=[*command,'-MD','-MF',str(dep),'-MT',str(obj)]
    compiler=Path(shutil.which(command[0]) or command[0])
    if str(compiler) not in compiler_hashes:compiler_hashes[str(compiler)]=sha(compiler)
    key=hashlib.sha256((json.dumps(command)+compiler_hashes[str(compiler)]).encode()).hexdigest()
    reuse=False
    if args.resume and stamp.exists() and obj.exists():
        previous=json.loads(stamp.read_text())
        reuse=previous['command_sha256']==key and previous['object_sha256']==sha(obj)
        if reuse:
            for name,digest in previous['dependencies'].items():
                path=Path(name)
                if not path.is_file() or sha(path)!=digest:reuse=False;break
    if reuse:
        report['steps'].append({'label':label,'argv':command,'exit_code':0,'reused_verified_dependencies':True})
        print(label,'reused',flush=True)
    else:
        run(label,command,build)
        text=dep.read_text().replace('\\\n','')
        names=shlex.split(text.partition(':')[2])
        dependencies={}
        for name in names:
            path=Path(name)
            if not path.is_absolute():path=build/path
            dependencies[str(path)]=sha(path)
        assert str(src) in dependencies,(label,src)
        stamp.write_text(json.dumps({'command_sha256':key,'object_sha256':sha(obj),'dependencies':dependencies},indent=2)+'\n')
    report['sources'][str(src)]=sha(src);report['objects'][str(obj)]=sha(obj)
    report.setdefault('dependency_stamps',{})[str(stamp)]=sha(stamp);save()
steps=[s for s in old['steps'] if s['label'].startswith('compile-') and s['label']!='compile-image-headless']
if args.fixture_only:steps=[]
for step in steps:
    command=[remap(a) for a in step['argv']]
    # Database commands carry Ninja dependency-output paths. A standalone shim
    # compilation must not overwrite the engine's .d files or target metadata.
    cleaned=[];i=0
    while i<len(command):
        if command[i] in ['-MF','-MT','-MQ']:i+=2;continue
        if command[i] in ['-MD','-MMD','-MP']:i+=1;continue
        cleaned.append(command[i]);i+=1
    command=cleaned
    if step['label']=='compile-headless':command[1:1]=['-DDIHOS_INDEXEDDB_PROVIDER=1']
    compile_object(step['label'],command)
    if step['label']=='compile-headless':
        db_command=list(command)
        db_command[db_command.index('-c')+1]=str(OUT/'platform/DatabaseProviderDihOS.cpp')
        db_command[db_command.index('-o')+1]=str(OUT/'database-provider.o')
        compile_object('compile-database-provider',db_command)
        vm_command=list(command)
        vm_command[vm_command.index('-c')+1]=str(ROOT/'tools/jsc_dihos/OSAllocatorDihOS.cpp')
        vm_command[vm_command.index('-o')+1]=str(OUT/'os-allocator.o')
        vm_command[1:1]=['-I'+str(ROOT/'sdk/sacx/include')]
        compile_object('compile-os-allocator',vm_command)
    if step['label']=='compile-allocator':
        platform_command=list(command)
        platform_command[platform_command.index('-c')+1]=str(ROOT/'sdk/sacx/user_runtime/allocator_platform.cpp')
        platform_command[platform_command.index('-o')+1]=str(OUT/'allocator-platform.o')
        platform_command[1:1]=['-I'+str(ROOT/'sdk/sacx/include')]
        compile_object('compile-allocator-platform',platform_command)
        abort_command=list(platform_command)
        abort_command[abort_command.index('-c')+1]=str(ROOT/'sdk/sacx/user_runtime/libcxx/platform.cpp')
        abort_command[abort_command.index('-o')+1]=str(OUT/'libcxx-platform.o')
        abort_command[1:1]=['-fno-omit-frame-pointer','-I'+str(ROOT/'sdk/sacx/user_runtime/include')]
        compile_object('compile-libcxx-platform',abort_command)
run('make-page-fixture',['python3',ROOT/('tools/webcore_dihos/media/make_mse_fixture.py' if args.mse else 'tools/webcore_dihos/media/make_page_fixture.py'),*(['--output',OUT.parent] if args.mse else [])])
command=[remap(a) for a in next(s['argv'] for s in old['steps'] if s['label']=='compile-headless')]
cleaned=[];i=0
while i<len(command):
    if command[i] in ['-MF','-MT','-MQ']:i+=2;continue
    if command[i] in ['-MD','-MMD','-MP']:i+=1;continue
    cleaned.append(command[i]);i+=1
command=cleaned;command[1:1]=['-DDIHOS_INDEXEDDB_PROVIDER=1','-DDIHOS_VIDEO_TEST_FIXTURE=1','-I'+str(OUT.parent)]
if args.mse:command[1:1]=['-DDIHOS_MSE_TEST_FIXTURE=1']
command[command.index('-o')+1]=str(OUT/'video-page-headless.o')
compile_object('compile-video-page',command)
document_object=None
if args.software_view_transitions:
    original=source/'Source/WebCore/dom/Document.cpp'
    patched=OUT/'platform/DocumentDihOS.cpp';data=patch_document(original.read_text())
    if not patched.exists() or patched.read_text()!=data:patched.write_text(data)
    commands=json.loads((build/'compile_commands.json').read_text())
    matches=[c for c in commands if Path(c['file']).name=='UnifiedSource-dom-7.cpp']
    assert len(matches)==1
    engine_command=matches[0];unit=Path(engine_command['file']);text=unit.read_text()
    assert text.count('"dom/Document.cpp"')==1
    includes=re.findall(r'^#include "([^"]+)"',text,re.M)
    assert includes and len(includes)==text.count('#include')
    text='\n'.join('#include "'+str(patched if n=='dom/Document.cpp' else source/'Source/WebCore'/n)+'"' for n in includes)+'\n'
    unified=OUT/'platform/UnifiedDocumentDihOS.cpp'
    if not unified.exists() or unified.read_text()!=text:unified.write_text(text)
    raw=shlex.split(engine_command['command']);clean=[];i=0
    while i<len(raw):
        if raw[i] in ['-MF','-MT','-MQ']:i+=2;continue
        if raw[i] in ['-MD','-MMD','-MP']:i+=1;continue
        clean.append(raw[i]);i+=1
    document_object=OUT/'document-software.o'
    clean[clean.index('-c')+1]=str(unified);clean[clean.index('-o')+1]=str(document_object)
    compile_object('compile-document-software',clean)
    report['software_view_transitions']={'upstream_document_sha256':sha(original),'upstream_unified_sha256':sha(unit),
        'patcher_sha256':sha(ROOT/'tools/webcore_dihos/software_view_transitions.py'),'patched_document_sha256':sha(patched),
        'object_sha256':sha(document_object)};save()
    transition_command=list(command);transition_command=[a for a in transition_command if a!='-DDIHOS_MSE_TEST_FIXTURE=1']
    transition_command[1:1]=['-DDIHOS_VIEW_TRANSITION_FIXTURE=1','-I'+str(ROOT/'tools/webcore_dihos')]
    transition_command[transition_command.index('-o')+1]=str(OUT/'view-transition-headless.o')
    compile_object('compile-view-transition',transition_command)
if args.compile_only or args.fixture_only:save();raise SystemExit(0)
assert engine['engine_built'],'Complete the video-enabled engine build first'
for name,digest in engine['libraries'].items():assert sha(Path(name))==digest,name
bytecode=OUT/'libJavaScriptCoreBytecode.a'
objects=sorted((build/'Source/JavaScriptCore/CMakeFiles/JavaScriptCoreJIT.dir').rglob('*.obj'))
assert objects
if bytecode.exists():bytecode.unlink()
run('archive-bytecode',['aarch64-linux-gnu-ar','rcs',bytecode,*objects])
decoder_dir=ROOT/'build/dihscover-media-validation'
decoder=json.loads((decoder_dir/'decoder-report.json').read_text())
for name,digest in decoder['libraries'].items():assert sha(Path(name))==digest,name
# Only the adapter and C support have a separate freestanding C ABI. They do
# not include any WebCore headers; use the exact validated native objects.
fixture=json.loads((decoder_dir/'fixture-report.json').read_text())
for name in ['memory-video.o']:
    path=decoder_dir/name;assert fixture['linked_inputs'][str(path)]==sha(path),name
support=ROOT/'tools/webcore_dihos/media/native_c_support.c'
run('compile-media-c-support',['clang-22',*decoder['compiler_flags'],'-DDIHOS_MEDIA_USE_EXISTING_C_HELPERS=1',
    '-c',support,'-o',OUT/'media-c-support.o'])
report['sources'][str(support)]=sha(support);report['objects'][str(OUT/'media-c-support.o')]=sha(OUT/'media-c-support.o')
media_objects=[]
if args.mse:
    for name in ['fragmented_mp4','encoded_media','sample_decoder']:
        path=ROOT/'tools/webcore_dihos/media'/(name+'.c');obj=OUT/(name+'.o')
        run('compile-'+name,['clang-22',*decoder['compiler_flags'],'-I'+decoder['build'],'-I'+decoder['source'],'-c',path,'-o',obj])
        report['sources'][str(path)]=sha(path);report['objects'][str(obj)]=sha(obj);media_objects.append(str(obj))
link=[remap(a) for a in next(s['argv'] for s in old['steps'] if s['label']=='link')]
for i,value in enumerate(link):
    match=re.search(r'/[0-9a-f]{64}-(lib(?:WebCore|PAL|JavaScriptCore|JavaScriptCoreBytecode|WTF|bmalloc)\.a)$',value)
    if match:link[i]=str(bytecode if match[1]=='libJavaScriptCoreBytecode.a' else build/'lib'/match[1])
insert=link.index('--start-group')
link[insert:insert]=[str(OUT/'allocator-platform.o'),str(OUT/'libcxx-platform.o'),str(OUT/'database-provider.o'),str(OUT/'os-allocator.o'),str(decoder_dir/'memory-video.o'),str(OUT/'media-c-support.o'),*media_objects]
if document_object:link.insert(link.index('--start-group'),str(document_object))
insert=link.index('--end-group');link[insert:insert]=list(decoder['libraries'])
report['linked_inputs']={a:sha(Path(a)) for a in link if a.endswith(('.o','.a','.ld'))}
run('link',link)
fixture_link=[str(OUT/'video-page-headless.o') if a==str(OUT/'headless.o') else a for a in link]
fixture_link[fixture_link.index('-o')+1]=str(OUT/'video-page.elf')
run('link-video-page',fixture_link)
resource_link=[str(OUT/'resource-fixture.o') if a==str(OUT/'headless.o') else a for a in link]
resource_link=[a for a in resource_link if a not in [str(OUT/'browser_embedded.o'),str(OUT/'navigation_input.o')]]
resource_link[resource_link.index('-o')+1]=str(OUT/'resource.elf')
run('link-resource',resource_link)
if document_object:
    transition_link=[str(OUT/'view-transition-headless.o') if a==str(OUT/'headless.o') else a for a in link]
    transition_link[transition_link.index('-o')+1]=str(OUT/'view-transition.elf')
    run('link-view-transition',transition_link)
    control=[a for a in transition_link if a!=str(document_object)]
    control[control.index('-o')+1]=str(OUT/'view-transition-control.elf');run('link-view-transition-control',control)
def segments(path):
    data=path.read_bytes();offset=struct.unpack_from('<Q',data,32)[0];count=struct.unpack_from('<H',data,56)[0]
    result=[]
    for i in range(count):
        kind,flags,start,address,physical,size,memory,alignment=struct.unpack_from('<IIQQQQQQ',data,offset+i*56)
        if kind==1:result.append((flags,address,size,memory,alignment,hashlib.sha256(data[start:start+size]).hexdigest()))
    return result
strip=shutil.which('llvm-strip-22') or shutil.which('aarch64-linux-gnu-strip');assert strip
for name in ['page','video-page']:
    path=OUT/(name+'.elf');symbols=OUT/(name+'.symbols.elf');shutil.copy2(path,symbols)
    before=segments(path);run('strip-'+name,[strip,'--strip-all',path]);assert segments(path)==before
    report.setdefault('symbols',{})[str(symbols)]=sha(symbols)
if document_object:
    for name in ['view-transition','view-transition-control']:
        path=OUT/(name+'.elf');before=segments(path);run('strip-'+name,[strip,'--strip-all',path]);assert segments(path)==before
        report.setdefault('transition_fixtures',{})[name]=sha(path)
run('inspector-build',['clang-22','-Ikernel/include','tools/inspect_user_elf.c','kernel/process/dihos_user_elf.c','-o',OUT/'inspect'])
elf=OUT/'page.elf';run('loader-check',[OUT/'inspect',elf])
run('loader-check-video-page',[OUT/'inspect',OUT/'video-page.elf'])
run('loader-check-resource',[OUT/'inspect',OUT/'resource.elf'])
run('imports',['aarch64-linux-gnu-nm','-u',OUT/'page.symbols.elf']);assert not (OUT/'imports.log').read_text().strip()
report.update(linked=True,elf_sha256=sha(elf),elf_bytes=elf.stat().st_size,video_page_sha256=sha(OUT/'video-page.elf'),fixture_elf_sha256=sha(OUT/'resource.elf'));save()
print('Video-enabled browser renderer linked and loader accepted; Page playback execution still required',flush=True)
