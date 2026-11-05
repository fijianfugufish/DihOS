"""Link the real WebCore resource fixture with verified native dependencies."""
from pathlib import Path
import hashlib,json,platform,shlex,subprocess
ROOT=Path(__file__).resolve().parents[2]
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
base_path=ROOT/'build/user-jsc-native/resource-report.json';base=json.loads(base_path.read_text())
config_path=ROOT/'build/webcore-native/report.json';config=json.loads(config_path.read_text())
if not base.get('linked') or not base.get('loader_accepted') or not config.get('webcore_configured'):
    raise SystemExit('Verified native resource fixture and WebCore configuration required')
for path,digest in base['libraries'].items():
    if sha(Path(path))!=digest:raise SystemExit('Changed target library: '+path)
for relative,digest in config['native_platform_sources'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Changed platform source')
if config['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/configure_native.py'):raise SystemExit('Reconfigure changed native builder')
database=Path(config['build'])/'compile_commands.json'
if sha(database)!=config['compile_commands_sha256']:raise SystemExit('Changed native compiler database')
entries=json.loads(database.read_text())
template=next(e for e in entries if e['file'].endswith('/platform/dihos/WebCoreResourceLoad.cpp'))
out=ROOT/'build/webcore-resource-bridge';out.mkdir(exist_ok=True)
fixture=ROOT/'tools/webcore_dihos/webcore_resource_fixture.cpp';obj=out/'fixture.o'
report={'target':'aarch64-none-elf','builder_sha256':sha(Path(__file__)),'fixture_sha256':sha(fixture),
        'base_report_sha256':sha(base_path),'configure_report_sha256':sha(config_path),
        'linked':False,'loader_accepted':False,'dom_executed':False,'page_rendered':False,'steps':[],
        'libraries':base['libraries'],'inputs':dict(config['native_platform_sources'])}
def save():(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label,argv,cwd=ROOT):
    with (out/(label+'.log')).open('w') as stream:code=subprocess.call([str(x) for x in argv],cwd=cwd,stdout=stream,stderr=subprocess.STDOUT)
    report['steps'].append({'label':label,'argv':[str(x) for x in argv],'exit_code':code});save()
    if code:raise SystemExit(label+' failed; inspect '+str(out/(label+'.log')))
units=['platform/dihos/WebCoreResourceLoad.cpp','platform/network/ResourceRequestBase.cpp',
       'platform/network/ResourceResponseBase.cpp','platform/network/ResourceErrorBase.cpp',
       'platform/network/HTTPHeaderMap.cpp','platform/network/HTTPParsers.cpp',
       'platform/network/FormData.cpp','derived/HTTPHeaderNames.cpp']
units_path=ROOT/'build/webcore-native/units/report.json'
cached=json.loads(units_path.read_text()) if units_path.exists() else {}
reuse=(cached.get('configure_report_sha256')==sha(config_path) and cached.get('all_selected_units_compiled') and len(cached.get('units',[]))==len(units))
if reuse:
    for unit,entry in zip(units,cached['units']):
        if not entry['object'].endswith('/'+unit.replace('/','_')+'.o') or sha(Path(entry['source']))!=entry['source_sha256'] or sha(Path(entry['object']))!=entry['object_sha256']:
            reuse=False;break
if not reuse:run('compile-units',['python3',ROOT/'tools/webcore_dihos/compile_units.py',*[item for unit in units for item in ['--unit',unit]]])
report['units_report_sha256']=sha(units_path)
command=shlex.split(template['command']);command[command.index('-c')+1]=str(fixture);command[command.index('-o')+1]=str(obj)
run('compile-fixture',command,Path(template['directory']))
names=[unit.replace('/','_')+'.o' for unit in units]
objects=[ROOT/'build/webcore-native/units'/name for name in names]
report['objects']={str(p):sha(p) for p in objects}
link=list(next(s for s in base['steps'] if s['label']=='link')['argv'])
old_obj=next(a for a in link if a.endswith('/embedding.o'));link[link.index(old_obj)]=str(obj)
link[link.index('--start-group'):link.index('--start-group')]=[str(p) for p in objects]
elf=out/'webcore-resource.elf';link[link.index('-o')+1]=str(elf)
report['linked_inputs']={str(Path(a)):sha(Path(a)) for a in link if a.endswith(('.o','.a'))}
run('link',link);report['linked']=True;report['elf_sha256']=sha(elf);report['elf_bytes']=elf.stat().st_size;save()
inspector=out/'inspect'
run('inspector-build',['clang-22','-Ikernel/include','tools/inspect_user_elf.c','kernel/process/dihos_user_elf.c','-o',inspector])
run('loader-check',[inspector,elf]);report['loader_accepted']=True
run('imports',['aarch64-linux-gnu-nm','-u',elf])
if (out/'imports.log').read_text().strip():raise SystemExit('Native ELF contains unresolved imports')
for path,digest in report['linked_inputs'].items():
    if sha(Path(path))!=digest:raise SystemExit('Input changed during link')
save();print('Linked real WebCore resource fixture; production ELF loader accepts it')
