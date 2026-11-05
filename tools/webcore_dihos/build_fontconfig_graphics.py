"""Cairo's real Fontconfig backend, using only verified native libraries."""
from pathlib import Path
import ast,hashlib,json,os,platform,shutil,subprocess
ROOT=Path(__file__).resolve().parents[2]
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
fcpath=ROOT/'build/webcore-fontconfig/report.json';fc=json.loads(fcpath.read_text())
fontpath=ROOT/'build/webcore-font-graphics/report.json';font=json.loads(fontpath.read_text())
graphics=json.loads((ROOT/'build/webcore-graphics/report.json').read_text())
if not fc.get('library_built') or fc['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_fontconfig.py'):raise SystemExit('Build verified native Fontconfig first')
if fc['font_report_sha256']!=sha(fontpath) or font['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_font_graphics.py'):raise SystemExit('Changed native font profile')
dependencies={fc['library']:fc['library_sha256'],fc['freetype_library']:fc['freetype_library_sha256'],**font['libraries']}
for p,digest in dependencies.items():
    if sha(Path(p))!=digest:raise SystemExit('Changed native font library')
for relative,digest in {**fc['inputs'],**font['inputs']}.items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Changed native adapter')
cache=Path.home()/'.cache/dihos-webcore-fonts';stage=cache/'fontconfig-graphics-stage';stage.mkdir(exist_ok=True)
crossbase=cache/'graphics.ini';lines=crossbase.read_text().splitlines()
pkgdirs=[str(Path(fc['stage'])/'lib/pkgconfig'),str(Path(font['stage'])/'lib/pkgconfig')]
for i,line in enumerate(lines):
    if line.startswith('pkg_config_libdir ='):lines[i]='pkg_config_libdir = '+repr(pkgdirs)
    if line.startswith('c_args ='):
        flags=ast.literal_eval(line.split('=',1)[1].strip())+['-include',str(ROOT/'tools/webcore_dihos/fontconfig_cairo_io.h')]
        lines[i]='c_args = '+repr(flags)
cross=cache/'fontconfig-graphics.ini';cross.write_text('\n'.join(lines)+'\n')
env=dict(os.environ,PKG_CONFIG_LIBDIR=':'.join(pkgdirs),PKG_CONFIG_PATH='')
source=Path(graphics['cache'])/'cairo-native-1.18.6-v1';build=cache/'cairo-fontconfig-build'
out=ROOT/'build/webcore-fontconfig-graphics';out.mkdir(exist_ok=True)
report={'target':'aarch64-none-elf','builder_sha256':sha(Path(__file__)),'fontconfig_report_sha256':sha(fcpath),'font_graphics_report_sha256':sha(fontpath),
        'libraries_built':False,'steps':[],'source':str(source),'cross_file_sha256':sha(cross),
        'inputs':{'tools/webcore_dihos/fontconfig_cairo_io.h':sha(ROOT/'tools/webcore_dihos/fontconfig_cairo_io.h')}}
def run(label,argv):
    with (out/(label+'.log')).open('w') as stream:code=subprocess.call([str(x) for x in argv],env=env,stdout=stream,stderr=subprocess.STDOUT,cwd=ROOT)
    report['steps'].append({'label':label,'argv':[str(x) for x in argv],'exit_code':code});(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(label,code,flush=True)
    if code:raise SystemExit(label+' failed')
command=['meson','setup',str(build),str(source),'--cross-file',str(cross),'--prefix',str(stage),'--libdir','lib','--wrap-mode=nofallback','-Dauto_features=disabled','-Dbuildtype=release','-Dfreetype=enabled','-Dfontconfig=enabled']
command+=['-D'+name+'=disabled' for name in ['dwrite','png','quartz','tee','xcb','xlib','xlib-xcb','zlib','tests','lzo','gtk2-utils','glib','spectre','symbol-lookup']]
if (build/'meson-private/coredata.dat').exists():command.insert(2,'--wipe')
run('configure',command);run('build',['meson','compile','-C',build,'-j','4']);run('install',['meson','install','-C',build,'--no-rebuild'])
lib=stage/'lib/libcairo.a';report.update(libraries_built=True,library=str(lib),library_sha256=sha(lib),stage=str(stage))
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print('Native Cairo Fontconfig backend built; WebCore font integration not yet executed')
