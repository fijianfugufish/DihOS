"""Link the local Page against one complete native engine configuration.

Requires a successful WebCore archive build and a current client compile.
Never falls back to the older, separately configured resource-only JSC build.
Linking/loader acceptance alone is not proof of DOM execution or rendering.
"""
from pathlib import Path
import argparse,hashlib,json,platform,shlex,shutil,subprocess
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--report',type=Path,default=ROOT/'build/webcore-engine-native/report.json');parser.add_argument('--strip',action='store_true');args=parser.parse_args()
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=ROOT/'build/webcore-headless-page';out.mkdir(exist_ok=True)
config_path=args.report.resolve();config=json.loads(config_path.read_text())
built_path=config_path.parent/'compile-report.json';built=json.loads(built_path.read_text())
client_path=out/'compile-report.json';client=json.loads(client_path.read_text())
font_path=ROOT/'build/user-font-select/report.json';fonts=json.loads(font_path.read_text())
base_path=ROOT/'build/user-jsc-native/resource-report.json';base=json.loads(base_path.read_text())
if config['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/configure_native.py') or not config.get('webcore_configured'):
    raise SystemExit('Reconfigure the native engine')
for field,name in [('webcore_edits_sha256','port-edits.json'),('image_edits_sha256','image-edits.json'),('engine_edits_sha256','engine-edits.json')]:
    if config[field]!=sha(ROOT/'tools/webcore_dihos'/name):raise SystemExit('Port edits changed; reconfigure')
if built['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/compile_native.py') or built['configure_report_sha256']!=sha(config_path) or built['exit_code'] or not all(t in built['argv'] for t in ['WebCore','JavaScriptCoreJIT']):
    raise SystemExit('Complete the native WebCore archive build first')
if client['engine_report_sha256']!=sha(config_path) or client['exit_code'] or client['object_sha256']!=sha(out/'headless.o'):
    raise SystemExit('Compile the current native Page client first')
for key,name in [('fixture_sha256','headless_page_fixture.cpp'),('strategies_sha256','headless_page_strategies.h'),('platform_sha256','headless_page_platform.cpp')]:
    if client[key]!=sha(ROOT/'tools/webcore_dihos'/name):raise SystemExit('Page client changed; recompile')
for path,digest in {**client['support_sources'],**client['support_objects']}.items():
    if sha(Path(path))!=digest:raise SystemExit('Page support changed; recompile')
for relative,digest in client.get('navigation_inputs',{}).items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Navigation source changed; recompile')
if client['window_header_sha256']!=sha(ROOT/'sdk/sacx/include/sacx_user_window.h'):raise SystemExit('Window ABI changed; recompile')
for relative,digest in config['platform_files'].items():
    if sha(Path(config['source'])/relative)!=digest:raise SystemExit('Engine platform changed')
for relative,digest in config['native_platform_sources'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Engine adapter changed')
if not fonts.get('loader_accepted') or not base.get('loader_accepted'):
    raise SystemExit('Verified native runtime and font inputs required')
for report in [fonts,base]:
    for path,digest in report['libraries'].items():
        if sha(Path(path))!=digest:raise SystemExit('Changed native library: '+path)
for relative,digest in fonts['inputs'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Changed packaged font/ABI input: '+relative)
crypto=None
if config.get('openssl_enabled'):
    p=ROOT/'build/webcore-openssl/report.json';crypto=json.loads(p.read_text())
    if sha(p)!=config['openssl_report_sha256'] or sha(ROOT/'build/webcore-openssl/validation.json')!=config['openssl_validation_sha256']:raise SystemExit('Changed native crypto validation')
    if sha(Path(crypto['library']))!=crypto['library_sha256']:raise SystemExit('Changed native crypto archive')
    for relative,digest in crypto['inputs'].items():
        if sha(ROOT/relative)!=digest:raise SystemExit('Changed native crypto adapter')

report={'target':'aarch64-none-elf','builder_sha256':sha(Path(__file__)),
        'engine_report_sha256':sha(config_path),'engine_compile_report_sha256':sha(built_path),
        'client_compile_report_sha256':sha(client_path),'font_report_sha256':sha(font_path),
        'base_report_sha256':sha(base_path),'steps':[], 'sealed_archives':{},
        'capture':client.get('capture',False),'window':client.get('window',False),'navigation':client.get('navigation',False),'https':client.get('https',False),'embedded':client.get('embedded',False),'linked':False,'loader_accepted':False,'dom_executed':False,
        'page_rendered':False,'shipping_browser_changed':False}
def save():(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label,argv,**kwargs):
    with (out/(label+'.log')).open('w') as stream:
        code=subprocess.call([str(x) for x in argv],cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT,**kwargs)
    report['steps'].append({'label':label,'argv':[str(x) for x in argv],'exit_code':code});save()
    if code:raise SystemExit(label+' failed; see '+str(out/(label+'.log')))

assembly=[]
for family in ['Sans','Serif','Mono']:
    path=ROOT/'OS/System/Fonts/Web'/(family+'.ttf');symbol='font_'+family.lower()
    assembly.append(f'.section .rodata.{symbol},"a"\n.balign 16\n.global {symbol}\n.global {symbol}_end\n{symbol}:\n.incbin "{path}"\n{symbol}_end:\n')
(out/'font-data.S').write_text('\n'.join(assembly))
run('font-data',['clang-22','--target=aarch64-none-elf','-fpie','-c',out/'font-data.S','-o',out/'font-data.o'])

# CMake can produce thin archives. Flatten them so their recorded hashes cover
# the object bytes actually linked, not merely an index of mutable paths.
sealed=Path.home()/'.cache/dihos-webcore-native/sealed-page-archives';sealed.mkdir(exist_ok=True)
archiver=shutil.which('ar')
if not archiver:raise SystemExit('Native archive tool required')
report['archiver']={'path':archiver,'sha256':sha(Path(archiver))}
archives=[]
for name in ['WebCore','PAL','JavaScriptCore','WTF','bmalloc','JavaScriptCoreBytecode']:
    source=Path(config['build'])/'lib'/('lib'+name+'.a')
    if name!='JavaScriptCoreBytecode' and not source.is_file():raise SystemExit('Missing configured native archive: '+str(source))
    temporary=sealed/('page-'+name+'.a')
    recipe=out/('seal-'+name+'.mri')
    if name=='JavaScriptCoreBytecode':
        members=[]
        for entry in json.loads((Path(config['build'])/'compile_commands.json').read_text()):
            command=shlex.split(entry['command'])
            if '-o' not in command:continue
            member=Path(command[command.index('-o')+1])
            if 'JavaScriptCoreJIT.dir' in member.parts and member.suffix in ('.o','.obj'):
                members.append((Path(entry['directory'])/member).resolve())
        if not members or not all(p.is_file() for p in members):raise SystemExit('Build native JavaScriptCoreJIT objects first')
        recipe.write_text(f'CREATE {temporary}\n'+''.join(f'ADDMOD {p}\n' for p in members)+'SAVE\nEND\n')
    else:recipe.write_text(f'CREATE {temporary}\nADDLIB {source}\nSAVE\nEND\n')
    with recipe.open('r') as stream:run('seal-'+name,[archiver,'-M'],stdin=stream)
    if temporary.read_bytes()[:8]!=b'!<arch>\n':raise SystemExit('Sealed archive is not self-contained')
    digest=sha(temporary);destination=sealed/(digest+'-lib'+name+'.a')
    if destination.exists():
        if sha(destination)!=digest:raise SystemExit('Sealed archive collision')
        temporary.unlink()
    else:temporary.rename(destination)
    archives.append(str(destination))
    report['sealed_archives'][str(destination)]=digest
    save()

# Reuse the verified runtime/font link recipe, replacing every engine archive
# as a set. Font fixture-only wrappers are not part of the Page executable.
link=list(next(s for s in fonts['steps'] if s['label']=='link')['argv'])
link=[a for a in link if not a.startswith('--wrap=dihos_fc_')]
old=next(a for a in link if a.endswith('/fonts.o'));link[link.index(old)]=str(out/'headless.o')
old=next(a for a in link if a.endswith('/font-data.o'));link[link.index(old)]=str(out/'font-data.o')
engine_names=['WTF','JavaScriptCore','JavaScriptCoreBytecode','bmalloc']
link=[a for a in link if not any(a.endswith('-lib'+name+'.a') for name in engine_names)]
link[link.index('--start-group')+1:link.index('--start-group')+1]=archives
link[link.index('--start-group'):link.index('--start-group')]=list(client['support_objects'])
dependencies=json.loads((ROOT/'build/webcore-dependencies/report.json').read_text())
if sha(ROOT/'build/webcore-dependencies/report.json')!=config['dependency_report_sha256']:
    raise SystemExit('Changed native engine dependencies')
for library in dependencies['libraries'].values():
    if sha(Path(library['path']))!=library['sha256']:raise SystemExit('Changed native dependency')
link[link.index('--end-group'):link.index('--end-group')]=[v['path'] for v in dependencies['libraries'].values()]
if crypto:link.insert(link.index('--end-group'),crypto['library'])
elf=out/'page.elf';link[link.index('-o')+1]=str(elf)
report['linked_inputs']={a:sha(Path(a)) for a in link if a.endswith(('.o','.a','.ld'))}
run('link',link)
symbols=elf
if args.strip:
    # Non-loadable symbol tables cost disk reads but provide nothing to EL0.
    # Retain the complete symbol ELF for address diagnostics and prove the
    # loadable segments are byte-identical after stripping.
    import struct
    symbols=out/'page.symbols.elf';shutil.copyfile(elf,symbols)
    strip=shutil.which('llvm-strip-22') or shutil.which('aarch64-linux-gnu-strip')
    if not strip:raise SystemExit('Native AArch64 strip tool required')
    def segments(path):
        data=path.read_bytes();offset=struct.unpack_from('<Q',data,32)[0];count=struct.unpack_from('<H',data,56)[0]
        result=[]
        for i in range(count):
            kind,flags,at,address,physical,size,memory,alignment=struct.unpack_from('<IIQQQQQQ',data,offset+i*56)
            if kind==1:result.append((flags,address,size,memory,alignment,hashlib.sha256(data[at:at+size]).hexdigest()))
        return result
    before=segments(symbols);run('strip',[strip,'--strip-all',elf])
    if segments(elf)!=before:raise SystemExit('Strip changed loadable image')
    report.update(stripped=True,symbols_sha256=sha(symbols),unstripped_bytes=symbols.stat().st_size,strip_tool_sha256=sha(Path(strip)))
report.update(linked=True,elf_sha256=sha(elf),elf_bytes=elf.stat().st_size);save()
run('inspector-build',['clang-22','-Ikernel/include','tools/inspect_user_elf.c','kernel/process/dihos_user_elf.c','-o',out/'inspect'])
run('loader-check',[out/'inspect',elf]);report['loader_accepted']=True;save()
run('imports',['aarch64-linux-gnu-nm','-u',symbols])
if (out/'imports.log').read_text().strip():raise SystemExit('Unresolved native Page imports')
for path,digest in report['linked_inputs'].items():
    if sha(Path(path))!=digest:raise SystemExit('Input changed during link')
save();print('Native local Page linked and accepted by production loader; execution still required')
