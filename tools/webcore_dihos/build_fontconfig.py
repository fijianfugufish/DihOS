"""Pinned Fontconfig for native memory-font matching, with denied host I/O."""
from pathlib import Path
import argparse,ast,hashlib,json,os,platform,shutil,subprocess,tarfile,zipfile
parser=argparse.ArgumentParser();parser.add_argument('--reuse-config',action='store_true');args=parser.parse_args()
ROOT=Path(__file__).resolve().parents[2]
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
archive=ROOT/'build/browser-engine-arm64/fontconfig-2.16.0.tar.xz'
pin='6a33dc555cc9ba8b10caf7695878ef134eeb36d0af366041f639b1da9b6ed220'
if sha(archive)!=pin:raise SystemExit('Official Fontconfig checksum mismatch')
cache=Path.home()/'.cache/dihos-webcore-fontconfig';cache.mkdir(exist_ok=True)
original=cache/'fontconfig-2.16.0'
if not original.exists():
    with tarfile.open(archive) as t:t.extractall(cache,filter='data')
source=cache/'native-2.16.0'
if not source.exists():shutil.copytree(original,source)
mutex=(original/'src/fcmutex.h').read_text()
if mutex.count('#if 0')!=1:raise SystemExit('Fontconfig mutex selector changed')
mutex=mutex.replace('#if 0','#if defined(__DIHOS__)\n/* The forced native header supplies checked SACX wait/wake mutexes. */')
if (source/'src/fcmutex.h').read_text()!=mutex:(source/'src/fcmutex.h').write_text(mutex)
compat=(original/'src/fccompat.c').read_text()
if compat.count('#if HAVE_RANDOM_R')!=1:raise SystemExit('Fontconfig RNG selector changed')
compat=compat.replace('#if HAVE_RANDOM_R','#if defined(__DIHOS__)\n    result = dihos_fc_random ();\n#elif HAVE_RANDOM_R')
if (source/'src/fccompat.c').read_text()!=compat:(source/'src/fccompat.c').write_text(compat)
xml_source=(original/'src/fcxml.c').read_text()
if xml_source.count('#ifndef __BIONIC__')!=2:raise SystemExit('Fontconfig locale parser changed')
xml_source=xml_source.replace('#ifndef __BIONIC__','#if !defined(__BIONIC__) && !defined(__DIHOS__)')
if (source/'src/fcxml.c').read_text()!=xml_source:(source/'src/fcxml.c').write_text(xml_source)
face_source=(original/'src/fcfreetype.c').read_text()
marker='\tchar buf[4];\n\tint fd = FcOpen ((char *) file, O_RDONLY);\n\tif (fd != -1 && read (fd, buf, 4))'
if face_source.count(marker)!=1:raise SystemExit('Fontconfig wrapper-format inspection changed')
face_source=face_source.replace(marker,'\tchar buf[4];\n#ifdef __DIHOS__\n\tint got_header = face->stream && !face->stream->read && face->stream->base && face->stream->size >= sizeof (buf);\n\tif (got_header)\n\t    memcpy (buf, face->stream->base, sizeof (buf));\n#else\n\tint fd = FcOpen ((char *) file, O_RDONLY);\n\tint got_header = fd != -1 && read (fd, buf, sizeof (buf)) == sizeof (buf);\n\tif (fd != -1)\n\t    close (fd);\n#endif\n\tif (got_header)')
marker='\n\tclose (fd);\n    }\n\n    if (wrapper)'
if face_source.count(marker)!=1:raise SystemExit('Fontconfig wrapper-format cleanup changed')
face_source=face_source.replace(marker,'\n    }\n\n    if (wrapper)')
if (source/'src/fcfreetype.c').read_text()!=face_source:(source/'src/fcfreetype.c').write_text(face_source)
# The compiler's forced native ABI headers precede the gperf source. They are
# not part of the table definition and must not be emitted a second time.
cutout=(original/'src/cutout.py').read_text();marker='        write = True\n        for l in input_lines:\n'
if cutout.count(marker)!=1:raise SystemExit('Fontconfig generator filter changed')
cutout=cutout.replace(marker,'        write = False\n        for l in input_lines:\n            if l.strip().startswith("%{"):\n                write = True\n')
if (source/'src/cutout.py').read_text()!=cutout:(source/'src/cutout.py').write_text(cutout)
font_path=ROOT/'build/webcore-font-graphics/report.json';fonts=json.loads(font_path.read_text())
xml_path=ROOT/'build/webcore-font-xml/report.json';xml=json.loads(xml_path.read_text())
graphics_path=ROOT/'build/webcore-graphics/report.json';graphics=json.loads(graphics_path.read_text())
if not fonts.get('libraries_built') or fonts['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_font_graphics.py'):raise SystemExit('Build native font graphics first')
if not xml.get('library_built') or sha(Path(xml['library']))!=xml['library_sha256']:raise SystemExit('Verified native XML required')
if xml['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_font_xml.py'):raise SystemExit('Changed native XML ABI recipe')
for path,digest in fonts['libraries'].items():
    if sha(Path(path))!=digest:raise SystemExit('Changed native font archive')
stage=cache/'stage';(stage/'lib/pkgconfig').mkdir(parents=True,exist_ok=True)
for name in ['freetype2','pixman-1']:
    pc=(Path(fonts['stage'])/'lib/pkgconfig'/(name+'.pc')).read_text()
    if name=='freetype2':pc=pc.replace('-L'+str(Path(fonts['stage'])/'lib'),'-L'+str(stage/'lib'))
    (stage/'lib/pkgconfig'/(name+'.pc')).write_text(pc)
(stage/'lib/pkgconfig/libxml-2.0.pc').write_text(f'Name: libxml2\nDescription: DihOS native memory XML\nVersion: 2.11.6\nLibs: {xml["library"]}\nCflags: -I{xml["stage"]}/include/libxml2\n')
base_cross=Path(graphics['cache'])/'native.ini'
if sha(base_cross)!=graphics['cross_file_sha256']:raise SystemExit('Changed native ABI configuration')
lines=base_cross.read_text().splitlines();abi=ROOT/'tools/webcore_dihos/fontconfig'
for i,line in enumerate(lines):
    if line.startswith('c_args ='):
        flags=ast.literal_eval(line.split('=',1)[1].strip())
        flags=['-I'+str(abi),'-include',str(abi/'native.h')]+flags
        lines[i]='c_args = '+repr(flags)
    if line.startswith('pkg_config_libdir ='):lines[i]=f'pkg_config_libdir = [{str(stage/"lib/pkgconfig")!r}]'
cross=cache/'native.ini';cross.write_text('\n'.join(lines)+'\n')
meson=(original/'src/meson.build').read_text();marker="fc_sources = ["
if meson.count(marker)!=1:raise SystemExit('Fontconfig source list changed')
meson=meson.replace(marker,marker+f'\n  {str(abi/"native.c")!r}, {str(abi/"memory.c")!r},')
if (source/'src/meson.build').read_text()!=meson:(source/'src/meson.build').write_text(meson)
out=ROOT/'build/webcore-fontconfig';out.mkdir(exist_ok=True)
report={'target':'aarch64-none-elf','version':'2.16.0','archive_sha256':pin,'builder_sha256':sha(Path(__file__)),
        'font_report_sha256':sha(font_path),'xml_report_path':str(xml_path.relative_to(ROOT)),'xml_report_sha256':sha(xml_path),'library_built':False,
        'filesystem_access':False,'memory_font_matching_tested':False,'inputs':{str(p.relative_to(ROOT)):sha(p) for p in abi.rglob('*') if p.is_file()},'steps':[]}
env=dict(os.environ,PKG_CONFIG_LIBDIR=str(stage/'lib/pkgconfig'),PKG_CONFIG_PATH='')
def run(label,argv):
    with (out/(label+'.log')).open('w') as stream:code=subprocess.call([str(x) for x in argv],env=env,stdout=stream,stderr=subprocess.STDOUT,cwd=ROOT)
    report['steps'].append({'label':label,'argv':[str(x) for x in argv],'exit_code':code});(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(label,code,flush=True)
    if code:raise SystemExit(label+' failed')
base_font_path=ROOT/'build/webcore-fonts/report.json';base_font=json.loads(base_font_path.read_text())
if sha(base_font_path)!=fonts['font_report_sha256']:raise SystemExit('Changed native font source profile')
ftarchive=ROOT/'build/browser-deps/freetype.zip'
if sha(ftarchive)!=base_font['pins']['freetype.zip'][0]:raise SystemExit('Changed FreeType archive')
ftsource=Path(fonts['stage']).parent/'freetype-VER-2-14-1/src/base/ftsnames.c'
with zipfile.ZipFile(ftarchive) as z:
    if ftsource.read_bytes()!=z.read('freetype-VER-2-14-1/src/base/ftsnames.c'):raise SystemExit('Modified FreeType metadata source')
ftflags=ast.literal_eval(next(line.split('=',1)[1].strip() for line in base_cross.read_text().splitlines() if line.startswith('c_args =')))
ftobj=cache/'ftsnames.o'
run('ft-sfnt-names',['clang-22',*['-I'+p for p in base_font['includes']],'-DFT2_BUILD_LIBRARY',*ftflags,'-c',ftsource,'-o',ftobj])
ftlibrary=stage/'lib/libfreetype.a';shutil.copyfile(Path(fonts['stage'])/'lib/libfreetype.a',ftlibrary)
run('ft-archive',['aarch64-linux-gnu-ar','rcs',ftlibrary,ftobj])
report.update(freetype_library=str(ftlibrary),freetype_library_sha256=sha(ftlibrary),freetype_extra_source_sha256=sha(ftsource))
build=cache/'build'
command=['meson','setup',str(build),str(source),'--cross-file',str(cross),'--prefix',str(stage),'--libdir','lib','--wrap-mode=nofallback',
         '-Dauto_features=disabled','-Dbuildtype=release','-Dxml-backend=libxml2','-Dtests=disabled','-Dtools=disabled','-Ddoc=disabled','-Dcache-build=disabled','-Dnls=disabled','-Diconv=disabled','-Dfontations=disabled','-Ddefault-fonts-dirs=[]','-Dadditional-fonts-dirs=[]']
if (build/'meson-private/coredata.dat').exists():command.insert(2,'--wipe')
if not args.reuse_config:run('configure',command)
run('build',['meson','compile','-C',build,'-j','4']);run('install',['meson','install','-C',build,'--no-rebuild'])
library=stage/'lib/libfontconfig.a';report.update(library_built=not args.reuse_config,needs_fresh_configuration=args.reuse_config,library=str(library),library_sha256=sha(library),stage=str(stage))
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print('Native Fontconfig archive built; memory matching not yet executed')
