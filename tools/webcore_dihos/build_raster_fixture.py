"""Link native Cairo rendering/thread/OOM checks without a host renderer."""
from pathlib import Path
import argparse,hashlib,json,platform,subprocess
parser=argparse.ArgumentParser();parser.add_argument('--critical-oom',action='store_true');args=parser.parse_args()
ROOT=Path(__file__).resolve().parents[2]
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
graphics_path=ROOT/'build/webcore-graphics/report.json';graphics=json.loads(graphics_path.read_text())
base_path=ROOT/'build/user-jsc-native/resource-report.json';base=json.loads(base_path.read_text())
if not graphics.get('libraries_built') or graphics['target']!='aarch64-none-elf':raise SystemExit('Verified native graphics required')
if graphics['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_graphics.py'):raise SystemExit('Changed graphics builder')
libraries={**base['libraries'],**graphics['libraries']}
for path,digest in libraries.items():
    if sha(Path(path))!=digest:raise SystemExit('Changed native archive: '+path)
    if Path(path).read_bytes()[:8]!=b'!<arch>\n':raise SystemExit('Self-contained archive required')
for relative,digest in graphics['native_sources'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Changed graphics ABI')
out=ROOT/('build/user-cairo-oom' if args.critical_oom else 'build/user-cairo');out.mkdir(exist_ok=True)
fixture=ROOT/'tools/webcore_dihos/graphics/raster_fixture.cpp';obj=out/'raster.o';elf=out/'raster.elf'
result={'target':'aarch64-none-elf','builder_sha256':sha(Path(__file__)),'fixture_sha256':sha(fixture),
        'fixture_profile':'critical-oom' if args.critical_oom else 'raster',
        'graphics_report_sha256':sha(graphics_path),'base_report_sha256':sha(base_path),
        'libraries':libraries,'native_sources':graphics['native_sources'],'linked':False,'loader_accepted':False,
        'page_rendered':False,'dom_executed':False,'steps':[]}
def save():(out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
def run(label,argv):
    with (out/(label+'.log')).open('w') as stream:code=subprocess.call([str(x) for x in argv],stdout=stream,stderr=subprocess.STDOUT,cwd=ROOT)
    result['steps'].append({'label':label,'argv':[str(x) for x in argv],'exit_code':code});save()
    if code:raise SystemExit(label+' failed; see '+str(out/(label+'.log')))
command=list(next(s for s in base['steps'] if s['label']=='compile-embedding')['argv'])
command[command.index('-c')+1]=str(fixture);command[command.index('-o')+1]=str(obj)
command[1:1]=['-I'+str(ROOT/'tools/webcore_dihos/graphics'),'-I'+str(Path(graphics['stage'])/'include/cairo')]
if args.critical_oom:command.insert(1,'-DDIHOS_CAIRO_CRITICAL_OOM=1')
run('compile',command)
link=list(next(s for s in base['steps'] if s['label']=='link')['argv'])
old=next(a for a in link if a.endswith('/embedding.o'));link[link.index(old)]=str(obj)
link=[a for a in link if not any(a.endswith('/'+name) for name in ['dispatch-queue.o','resource-load.o','RunLoopGeneric.o'])]
link[link.index('--end-group'):link.index('--end-group')]=list(graphics['libraries'])
link[1:1]=['--wrap=malloc','--wrap=calloc','--wrap=realloc']
link[link.index('-o')+1]=str(elf)
run('link',link);result['linked']=True;result['elf_sha256']=sha(elf);result['elf_bytes']=elf.stat().st_size
inspector=out/'inspect';run('inspector-build',['clang-22','-Ikernel/include','tools/inspect_user_elf.c','kernel/process/dihos_user_elf.c','-o',inspector])
run('loader-check',[inspector,elf]);result['loader_accepted']=True
run('imports',['aarch64-linux-gnu-nm','-u',elf])
if (out/'imports.log').read_text().strip():raise SystemExit('Unresolved native imports')
save();print('Native Cairo fixture linked and accepted by the production ELF loader')
