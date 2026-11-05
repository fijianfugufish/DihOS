"""Native Cairo FreeType backend; isolated from the tested image-only build."""
from pathlib import Path
import ast,hashlib,json,os,platform,shutil,subprocess
ROOT=Path(__file__).resolve().parents[2]
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
font_path=ROOT/'build/webcore-fonts/report.json';fonts=json.loads(font_path.read_text())
graphics_path=ROOT/'build/webcore-graphics/report.json';graphics=json.loads(graphics_path.read_text())
if not fonts.get('libraries_built') or fonts['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_fonts.py'):raise SystemExit('Verified native fonts required')
if fonts['graphics_report_sha256']!=sha(graphics_path):raise SystemExit('Changed native graphics')
for path,digest in {**fonts['libraries'],**graphics['libraries']}.items():
    if sha(Path(path))!=digest:raise SystemExit('Changed native archive')
for relative,digest in fonts['inputs'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Changed native ABI input')
cache=Path.home()/'.cache/dihos-webcore-fonts';out=ROOT/'build/webcore-font-graphics';out.mkdir(exist_ok=True)
stage=cache/'graphics-stage';(stage/'lib/pkgconfig').mkdir(parents=True,exist_ok=True)
native=Path(graphics['cache']);cross_text=(native/'native.ini').read_text()
if sha(native/'native.ini')!=graphics['cross_file_sha256']:raise SystemExit('Changed native compiler profile')
cflags=ast.literal_eval(next(line.split('=',1)[1].strip() for line in cross_text.splitlines() if line.startswith('c_args =')))
ft=cache/'freetype-VER-2-14-1';config=cache/'config'
cflags=['-I'+str(config),'-I'+str(ft/'include'),'-DFT2_BUILD_LIBRARY']+cflags
report={'target':'aarch64-none-elf','builder_sha256':sha(Path(__file__)),'font_report_sha256':sha(font_path),
        'graphics_report_sha256':sha(graphics_path),'libraries_built':False,'fontconfig_built':False,
        'filesystem_access':False,'page_rendered':False,'steps':[]}
def save():(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label,argv,env=None):
    with (out/(label+'.log')).open('w') as stream:code=subprocess.call([str(x) for x in argv],stdout=stream,stderr=subprocess.STDOUT,cwd=ROOT,env=env)
    report['steps'].append({'label':label,'argv':[str(x) for x in argv],'exit_code':code});save();print(label,code,flush=True)
    if code:raise SystemExit(label+' failed')
objects=[]
for name in ['ftfntfmt','ftlcdfil']:
    obj=cache/(name+'.o');objects.append(obj)
    run(name,['clang-22',*cflags,'-c',ft/'src/base'/(name+'.c'),'-o',obj])
# Extend a copy; leave the previously tested FreeType archive unchanged.
ftlib=stage/'lib/libfreetype.a';shutil.copyfile(cache/'libfreetype.a',ftlib)
run('ft-archive',['aarch64-linux-gnu-ar','rcs',ftlib,*objects])
version=subprocess.check_output(['python3',str(ft/'builds/meson/extract_libtool_version.py'),str(ft/'builds/unix/configure.raw')],text=True).strip()
(stage/'lib/pkgconfig/freetype2.pc').write_text(f'Name: FreeType 2\nDescription: Native DihOS memory-font build\nVersion: {version}\nLibs: -L{stage}/lib -lfreetype\nCflags: -I{config} -I{ft}/include\n')
shutil.copyfile(native/'stage/lib/libpixman-1.a',stage/'lib/libpixman-1.a')
shutil.copyfile(native/'stage/lib/libm.a',stage/'lib/libm.a')
pixman_pc=(native/'stage/lib/pkgconfig/pixman-1.pc').read_text()
(stage/'lib/pkgconfig/pixman-1.pc').write_text(pixman_pc.replace(str(native/'stage'),str(stage)))
shutil.copytree(native/'stage/include/pixman-1',stage/'include/pixman-1',dirs_exist_ok=True)
cross=cache/'graphics.ini';cross_text=cross_text.replace(str(native/'stage'),str(stage))
abi=ROOT/'tools/webcore_dihos/font_cairo_abi.h'
lines=cross_text.splitlines()
for i,line in enumerate(lines):
    if line.startswith('c_args ='):
        flags=ast.literal_eval(line.split('=',1)[1].strip())+['-include',str(abi)]
        lines[i]='c_args = '+repr(flags)
cross.write_text('\n'.join(lines)+'\n')
env=dict(os.environ,PKG_CONFIG_LIBDIR=str(stage/'lib/pkgconfig'),PKG_CONFIG_PATH='')
build=cache/'cairo-build';source=native/'cairo-native-1.18.6-v1'
report['native_cairo_sources']={str(p.relative_to(source)):sha(p) for p in (source/'src').iterdir() if p.suffix in ['.c','.h','.S']}
report['inputs']={str(abi.relative_to(ROOT)):sha(abi)}
command=['meson','setup',str(build),str(source),'--cross-file',str(cross),'--prefix',str(stage),'--libdir','lib','--wrap-mode=nofallback','-Dauto_features=disabled','-Dbuildtype=release','-Dfreetype=enabled']
command+=['-D'+name+'=disabled' for name in ['dwrite','fontconfig','png','quartz','tee','xcb','xlib','xlib-xcb','zlib','tests','lzo','gtk2-utils','glib','spectre','symbol-lookup']]
if (build/'meson-private/coredata.dat').exists():command.insert(2,'--wipe')
run('configure',command,env);run('build',['meson','compile','-C',build,'-j','4'],env);run('install',['meson','install','-C',build,'--no-rebuild'],env)
report.update(libraries_built=True,libraries={str(p):sha(p) for p in [stage/'lib/libcairo.a',ftlib]},stage=str(stage),freetype_pkgconfig_version=version)
save();print('Native Cairo FreeType backend built; shaped painting not yet executed')
