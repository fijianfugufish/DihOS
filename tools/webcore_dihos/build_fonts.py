"""Build pinned FreeType/HarfBuzz for native EL0, with real thread support."""
from pathlib import Path
import ast,hashlib,json,platform,shutil,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[2]
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
pins={'freetype.zip':('9ca6e4df3e5e2ccc4839ba55501e9168dbad7b32ef3b970cb681fd89479c75a3','freetype-VER-2-14-1'),
      'harfbuzz.zip':('8f90735337434ed3ed30e97f82f39799575cd79aa5ff841caab7971b3369d3c9','harfbuzz-11.4.5')}
cache=Path.home()/'.cache/dihos-webcore-fonts';cache.mkdir(exist_ok=True)
for filename,(digest,folder) in pins.items():
    archive=ROOT/'build/browser-deps'/filename
    if sha(archive)!=digest:raise SystemExit('Changed pinned font archive: '+filename)
    if not (cache/folder).exists():
        with zipfile.ZipFile(archive) as z:
            for name in z.namelist():
                if Path(name).is_absolute() or '..' in Path(name).parts:raise SystemExit('Unsafe font archive path')
            z.extractall(cache)
graphics_path=ROOT/'build/webcore-graphics/report.json';graphics=json.loads(graphics_path.read_text())
if not graphics.get('libraries_built'):raise SystemExit('Build native graphics first')
if graphics['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_graphics.py'):raise SystemExit('Changed graphics recipe')
for path,digest in graphics['libraries'].items():
    if sha(Path(path))!=digest:raise SystemExit('Changed native graphics library')
base_path=ROOT/'build/user-jsc-native/resource-report.json';base=json.loads(base_path.read_text())
for path,digest in base['libraries'].items():
    if sha(Path(path))!=digest:raise SystemExit('Changed native runtime library')
config=cache/'config/freetype/config';config.mkdir(parents=True,exist_ok=True)
inputs={}
for relative,digest in graphics['native_sources'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Changed native graphics ABI')
    inputs[relative]=digest
native_headers=cache/'native-headers';native_headers.mkdir(exist_ok=True)
(native_headers/'setjmp.h').write_bytes((ROOT/'tools/webcore_dihos/graphics/setjmp.h').read_bytes())
for name in ['ftoption.h','ftmodule.h']:
    path=ROOT/'sdk/sacx/apps/dihscover/port/freetype/config'/name
    (config/name).write_bytes(path.read_bytes());inputs[str(path.relative_to(ROOT))]=sha(path)
ft=cache/'freetype-VER-2-14-1';hb=cache/'harfbuzz-11.4.5'
native_cross=Path(graphics['cache'])/'native.ini'
if sha(native_cross)!=graphics['cross_file_sha256']:raise SystemExit('Changed native C ABI configuration')
cflags=ast.literal_eval(next(line.split('=',1)[1].strip() for line in native_cross.read_text().splitlines() if line.startswith('c_args =')))
cflags=['-I'+str(config.parents[1]),'-I'+str(ft/'include'),'-DFT2_BUILD_LIBRARY']+cflags
out=ROOT/'build/webcore-fonts';out.mkdir(exist_ok=True)
objdir=cache/'objects';objdir.mkdir(exist_ok=True)
report={'target':'aarch64-none-elf','builder_sha256':sha(Path(__file__)),'pins':pins,'inputs':inputs,
        'graphics_report_sha256':sha(graphics_path),'base_report_sha256':sha(base_path),
        'libraries_built':False,'thread_safe_harfbuzz':True,'filesystem_access':False,
        'fontconfig_built':False,'page_rendered':False,'steps':[]}
def save():(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label,argv):
    with (out/(label+'.log')).open('w') as stream:code=subprocess.call([str(x) for x in argv],stdout=stream,stderr=subprocess.STDOUT,cwd=ROOT)
    report['steps'].append({'label':label,'argv':[str(x) for x in argv],'exit_code':code});save()
    print(label,code,flush=True)
    if code:raise SystemExit(label+' failed')
sources=['base/ftbitmap.c','base/ftbase.c','base/ftinit.c','base/ftsystem.c','base/ftdebug.c','base/ftglyph.c','base/ftmm.c','base/ftsynth.c',
         'autofit/autofit.c','truetype/truetype.c','cff/cff.c','psaux/psaux.c','psnames/psnames.c','pshinter/pshinter.c','sfnt/sfnt.c','smooth/smooth.c','raster/raster.c']
objects=[]
for relative in sources:
    obj=objdir/(Path(relative).stem+'.o');objects.append(obj)
    run('ft-'+obj.stem,['clang-22',*cflags,'-c',ft/'src'/relative,'-o',obj])
ftlib=cache/'libfreetype.a';run('ft-archive',['aarch64-linux-gnu-ar','rcs',ftlib,*objects])
command=list(next(step for step in base['steps'] if step['label']=='compile-embedding')['argv'])
command[command.index('-c')+1]=str(hb/'src/harfbuzz.cc');command[command.index('-o')+1]=str(objdir/'harfbuzz.o')
command[1:1]=['-I'+str(native_headers),'-I'+str(config.parents[1]),'-I'+str(ft/'include'),'-I'+str(hb/'src'),
               '-DHAVE_FREETYPE=1','-DHAVE_ICU=1','-DHAVE_ATEXIT=1','-DHB_MUTEX_IMPL_STD_MUTEX=1',
               '-DHB_NO_MMAP=1','-DHB_NO_OPEN=1','-DHB_NO_GETENV=1','-DHB_NO_SETLOCALE=1']
run('harfbuzz',command)
hblib=cache/'libharfbuzz.a';run('hb-archive',['aarch64-linux-gnu-ar','rcs',hblib,objdir/'harfbuzz.o'])
command[command.index('-c')+1]=str(hb/'src/hb-icu.cc');command[command.index('-o')+1]=str(objdir/'harfbuzz-icu.o')
run('harfbuzz-icu',command)
iculib=cache/'libharfbuzz-icu.a';run('hb-icu-archive',['aarch64-linux-gnu-ar','rcs',iculib,objdir/'harfbuzz-icu.o'])
report.update(libraries_built=True,libraries={str(p):sha(p) for p in [ftlib,hblib,iculib]},
              includes=[str(native_headers),str(config.parents[1]),str(ft/'include'),str(hb/'src')])
save();print('Native FreeType/HarfBuzz archives built; userspace shaping not yet executed')
