"""Link a native memory-font fixture, with no host font/file dependencies."""
from pathlib import Path
import argparse,hashlib,json,platform,subprocess
parser=argparse.ArgumentParser();parser.add_argument('--paint',action='store_true');parser.add_argument('--fontconfig',action='store_true');args=parser.parse_args()
if args.fontconfig:args.paint=True
ROOT=Path(__file__).resolve().parents[2]
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
font_path=ROOT/'build/webcore-fonts/report.json';fonts=json.loads(font_path.read_text())
base_path=ROOT/'build/user-jsc-native/resource-report.json';base=json.loads(base_path.read_text())
graphics_path=ROOT/'build/webcore-graphics/report.json';graphics=json.loads(graphics_path.read_text())
if not fonts.get('libraries_built') or fonts['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_fonts.py'):raise SystemExit('Build verified native fonts first')
if fonts['graphics_report_sha256']!=sha(graphics_path) or fonts['base_report_sha256']!=sha(base_path):raise SystemExit('Changed font dependencies')
for relative,digest in fonts['inputs'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Changed font ABI input')
extra_libraries={**graphics['libraries'],**fonts['libraries']}
paint=None
if args.paint:
    paint_path=ROOT/'build/webcore-font-graphics/report.json';paint=json.loads(paint_path.read_text())
    if not paint.get('libraries_built') or paint['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_font_graphics.py'):raise SystemExit('Build native Cairo FreeType first')
    if paint['font_report_sha256']!=sha(font_path) or paint['graphics_report_sha256']!=sha(graphics_path):raise SystemExit('Changed font-paint dependencies')
    for relative,digest in paint['inputs'].items():
        if sha(ROOT/relative)!=digest:raise SystemExit('Changed font-paint ABI input')
    extra_libraries={p:d for p,d in extra_libraries.items() if Path(p).name not in ['libcairo.a','libfreetype.a']}
    extra_libraries.update(paint['libraries'])
selection=None
if args.fontconfig:
    selection_path=ROOT/'build/webcore-fontconfig/report.json';selection=json.loads(selection_path.read_text())
    if not selection.get('library_built') or selection['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_fontconfig.py'):raise SystemExit('Build verified native Fontconfig first')
    if selection['font_report_sha256']!=sha(paint_path):raise SystemExit('Changed Fontconfig native font dependency')
    for relative,digest in selection['inputs'].items():
        if sha(ROOT/relative)!=digest:raise SystemExit('Changed Fontconfig adapter')
    xml_path=ROOT/selection['xml_report_path'];xml=json.loads(xml_path.read_text())
    if selection['xml_report_sha256']!=sha(xml_path):raise SystemExit('Changed Fontconfig XML parser')
    selected_paint_path=ROOT/'build/webcore-fontconfig-graphics/report.json';selected_paint=json.loads(selected_paint_path.read_text())
    if not selected_paint.get('libraries_built') or selected_paint['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_fontconfig_graphics.py') or selected_paint['fontconfig_report_sha256']!=sha(selection_path):raise SystemExit('Build verified Cairo Fontconfig backend')
    for relative,digest in selected_paint['inputs'].items():
        if sha(ROOT/relative)!=digest:raise SystemExit('Changed matched-font painting adapter')
    extra_libraries={p:d for p,d in extra_libraries.items() if Path(p).name not in ['libfreetype.a','libcairo.a']}
    extra_libraries.update({selection['freetype_library']:selection['freetype_library_sha256'],selection['library']:selection['library_sha256'],xml['library']:xml['library_sha256']})
    extra_libraries[selected_paint['library']]=selected_paint['library_sha256']
libraries={**base['libraries'],**extra_libraries}
for path,digest in libraries.items():
    if sha(Path(path))!=digest:raise SystemExit('Changed target archive: '+path)
out=ROOT/('build/user-font-select' if args.fontconfig else 'build/user-font-paint' if args.paint else 'build/user-fonts');out.mkdir(exist_ok=True)
fixture=ROOT/'tools/webcore_dihos/font_fixture.cpp';elf=out/'fonts.elf'
inputs={str(fixture.relative_to(ROOT)):sha(fixture)}
if args.fontconfig:
    inputs.update(selection['inputs']);inputs.update(selected_paint['inputs'])
    for builder in ['build_fontconfig.py','build_fontconfig_graphics.py','build_font_xml.py']:
        inputs['tools/webcore_dihos/'+builder]=sha(ROOT/'tools/webcore_dihos'/builder)
assembly=[]
for family in ['Sans','Serif','Mono']:
    path=ROOT/'OS/System/Fonts/Web'/(family+'.ttf');inputs[str(path.relative_to(ROOT))]=sha(path)
    symbol='font_'+family.lower()
    assembly.append(f'.section .rodata.{symbol},"a"\n.balign 16\n.global {symbol}\n.global {symbol}_end\n{symbol}:\n.incbin "{path}"\n{symbol}_end:\n')
for path in (ROOT/'OS/System/Fonts/Web').glob('*OFL.txt'):inputs[str(path.relative_to(ROOT))]=sha(path)
(out/'font-data.S').write_text('\n'.join(assembly))
report={'target':'aarch64-none-elf','builder_sha256':sha(Path(__file__)),'inputs':inputs,
        'fixture_profile':'selection' if args.fontconfig else 'paint' if args.paint else 'shaping',
        'font_report_sha256':sha(font_path),'base_report_sha256':sha(base_path),'graphics_report_sha256':sha(graphics_path),
        'libraries':libraries,'linked':False,'loader_accepted':False,'page_rendered':False,'steps':[]}
if args.paint:report['font_graphics_report_sha256']=sha(paint_path)
if args.fontconfig:report.update(fontconfig_report_sha256=sha(selection_path),fontconfig_graphics_report_sha256=sha(selected_paint_path))
def save():(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label,argv):
    with (out/(label+'.log')).open('w') as stream:code=subprocess.call([str(x) for x in argv],stdout=stream,stderr=subprocess.STDOUT,cwd=ROOT)
    report['steps'].append({'label':label,'argv':[str(x) for x in argv],'exit_code':code});save()
    if code:raise SystemExit(label+' failed')
command=list(next(s for s in base['steps'] if s['label']=='compile-embedding')['argv'])
command[command.index('-c')+1]=str(fixture);command[command.index('-o')+1]=str(out/'fonts.o')
command[1:1]=['-I'+p for p in fonts['includes']]
if args.paint:command[1:1]=['-DDIHOS_FONT_PAINT=1','-I'+str(Path(selected_paint['stage'] if args.fontconfig else paint['stage'])/'include/cairo')]
if args.fontconfig:command[1:1]=['-DDIHOS_FONTCONFIG=1','-I'+str(Path(selection['stage'])/'include')]
run('compile',command)
run('font-data',['clang-22','--target=aarch64-none-elf','-fpie','-c',out/'font-data.S','-o',out/'font-data.o'])
link=list(next(s for s in base['steps'] if s['label']=='link')['argv'])
old=next(a for a in link if a.endswith('/embedding.o'));link[link.index(old)]=str(out/'fonts.o')
link=[a for a in link if not any(a.endswith('/'+name) for name in ['dispatch-queue.o','resource-load.o','RunLoopGeneric.o'])]
link[link.index('--end-group'):link.index('--end-group')]=[str(out/'font-data.o'),*extra_libraries]
link[link.index('-o')+1]=str(elf)
if args.fontconfig:link[1:1]=['--wrap=dihos_fc_'+name for name in ['stat','access','getcwd','realpath','open','opendir']]
run('link',link);report.update(linked=True,elf_sha256=sha(elf),elf_bytes=elf.stat().st_size)
run('inspector-build',['clang-22','-Ikernel/include','tools/inspect_user_elf.c','kernel/process/dihos_user_elf.c','-o',out/'inspect'])
run('loader-check',[out/'inspect',elf]);report['loader_accepted']=True
run('imports',['aarch64-linux-gnu-nm','-u',elf])
if (out/'imports.log').read_text().strip():raise SystemExit('Unresolved native font imports')
save();print('Native fonts fixture linked and accepted by production ELF loader')
