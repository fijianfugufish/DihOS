"""Pinned native Cairo/Pixman image backend; no host renderer or OS library."""
from pathlib import Path
import argparse,hashlib,json,os,platform,re,shlex,shutil,subprocess,tarfile
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--prepare',action='store_true');args=parser.parse_args()
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
pins={'cairo-1.18.6.tar.xz':('sha256','1c767308174337a74694da0f3ec069c271452163a1ef4540964c50c301f157d4'),
      'pixman-0.46.4.tar.gz':('sha512','10ddb88b51f5456c440d77a7b4230600b099e818378a9b55f715bbe5ec3d9f1e9da2124d28a2bd3377f1ab20af87e0ec4fa9dadaa20a2f1f880dd2dc7f27ca6c')}
cache=Path.home()/'.cache/dihos-webcore-graphics';cache.mkdir(exist_ok=True)
for name,(algorithm,pin) in pins.items():
    archive=ROOT/'build/browser-engine-arm64'/name
    if hashlib.new(algorithm,archive.read_bytes()).hexdigest()!=pin:raise SystemExit('Official source checksum mismatch: '+name)
    source=cache/name.removesuffix('.tar.xz').removesuffix('.tar.gz')
    if not source.exists():
        with tarfile.open(archive) as tar:tar.extractall(cache,filter='data')
out=ROOT/'build/webcore-graphics';out.mkdir(exist_ok=True)
report={'target':'aarch64-none-elf','builder_sha256':sha(Path(__file__)),'pins':pins,
        'cache':str(cache),'libraries_built':False,'page_rendered':False,'host_engine_reused':False}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
if args.prepare:
    print('Verified/extracted pinned Cairo and Pixman sources');raise SystemExit(0)
native=cache/'cairo-native-1.18.6-v1';original=cache/'cairo-1.18.6'
if not native.exists():shutil.copytree(original,native,symlinks=True)
pixman_source=cache/'pixman-native-0.46.4-v1';pixman_original=cache/'pixman-0.46.4'
if not pixman_source.exists():shutil.copytree(pixman_original,pixman_source,symlinks=True)
# Native ELFs use the tested userspace atexit registry, not ELF DT_FINI_ARRAY.
pixman=(pixman_original/'pixman/pixman.c').read_text()
marker='pixman_implementation_t *global_implementation;'
if pixman.count(marker)!=1:raise SystemExit('Pixman global implementation changed')
pixman=pixman.replace(marker,marker+'\n#if defined(__DIHOS__) && defined(TOOLCHAIN_SUPPORTS_ATTRIBUTE_DESTRUCTOR)\nstatic void pixman_destructor (void);\n#endif')
marker='    global_implementation = _pixman_choose_implementation ();'
if pixman.count(marker)!=1:raise SystemExit('Pixman constructor changed')
pixman=pixman.replace(marker,marker+'\n#if defined(__DIHOS__) && defined(TOOLCHAIN_SUPPORTS_ATTRIBUTE_DESTRUCTOR)\n    if (atexit (pixman_destructor)) abort ();\n#endif')
marker='static void __attribute__((destructor))'
if pixman.count(marker)!=1:raise SystemExit('Pixman destructor changed')
pixman=pixman.replace(marker,'#if defined(__DIHOS__)\nstatic void\n#else\n'+marker+'\n#endif')
if (pixman_source/'pixman/pixman.c').read_text()!=pixman:(pixman_source/'pixman/pixman.c').write_text(pixman)
# Keep SP aligned at every preemption point, including leaf NEON routines.
# Saved registers remain addressed from x29; only unused frame padding changes.
relative='pixman/pixman-arma64-neon-asm.h'
assembly=(pixman_original/relative).read_text()
for before,after in [('sub         sp,   sp, 232','sub         sp,   sp, 240'),('sub         sp, sp, 88','sub         sp, sp, 96')]:
    if assembly.count(before)!=1:raise SystemExit('Pixman ARM64 stack frame changed')
    assembly=assembly.replace(before,after)
if (pixman_source/relative).read_text()!=assembly:(pixman_source/relative).write_text(assembly)
relative='pixman/pixman-arma64-neon-asm-bilinear.S'
assembly=(pixman_original/relative).read_text()
before='sub      sp, sp, 120'
if assembly.count(before)!=1:raise SystemExit('Pixman bilinear stack frame changed')
assembly=assembly.replace(before,'sub      sp, sp, 128')
if (pixman_source/relative).read_text()!=assembly:(pixman_source/relative).write_text(assembly)
mutex='src/cairo-mutex-impl-private.h'
text=(original/mutex).read_text()
marker='#elif defined(_WIN32) /******************************************************/'
if text.count(marker)!=1:raise SystemExit('Cairo mutex selector changed')
updated=text.replace(marker,'#elif defined(__DIHOS__)\n# include "cairo_mutex_dihos.h"\n\n'+marker)
if (native/mutex).read_text()!=updated:(native/mutex).write_text(updated)
for name in ['cairo_mutex_dihos.h','cairo_mutex_dihos.c','cairo_c_support.c']:
    data=(ROOT/'tools/webcore_dihos/graphics'/name).read_bytes();target=native/'src'/name
    if not target.exists() or target.read_bytes()!=data:target.write_bytes(data)
misc=(original/'src/cairo-misc.c').read_text()
marker='#ifndef __BIONIC__'
if misc.count(marker)!=1:raise SystemExit('Cairo locale selector changed')
misc=misc.replace(marker,'#if !defined(__BIONIC__) && !defined(__DIHOS__)')
marker='_cairo_tmpfile (void)\n{\n#ifdef _WIN32'
if misc.count(marker)!=1:raise SystemExit('Cairo temporary-file hook changed')
misc=misc.replace(marker,'_cairo_tmpfile (void)\n{\n#if defined(__DIHOS__)\n    errno = EACCES;\n    return NULL;\n#elif defined(_WIN32)')
if (native/'src/cairo-misc.c').read_text()!=misc:(native/'src/cairo-misc.c').write_text(misc)
clip=(original/'src/cairo-clip.c').read_text()
marker='\tclip = _cairo_calloc (sizeof (cairo_clip_t));\n\tif (unlikely (clip == NULL))\n\t    return NULL;'
if clip.count(marker)!=1:raise SystemExit('Cairo clip allocation contract changed')
clip=clip.replace(marker,'\tclip = _cairo_calloc (sizeof (cairo_clip_t));\n\tif (unlikely (clip == NULL)) {\n#ifdef __DIHOS__\n            extern void dihos_cairo_clip_allocation_failed (void);\n            dihos_cairo_clip_allocation_failed ();\n#endif\n\t    return NULL;\n        }')
if (native/'src/cairo-clip.c').read_text()!=clip:(native/'src/cairo-clip.c').write_text(clip)
musl=ROOT/'build/musl-1.2.5.tar.gz'
if sha(musl)!='a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4':raise SystemExit('Changed pinned native C source archive')
with tarfile.open(musl) as archive:
    for name in ['setjmp','longjmp']:
        member=archive.getmember('musl-1.2.5/src/setjmp/aarch64/'+name+'.s')
        target=native/'src'/('dihos-'+name+'.S');data=archive.extractfile(member).read()
        if not target.exists() or target.read_bytes()!=data:target.write_bytes(data)
meson=(original/'src/meson.build').read_text()
marker="cairo_sources = ["
if meson.count(marker)!=1:raise SystemExit('Cairo source list changed')
meson=meson.replace(marker,marker+"\n  'cairo_mutex_dihos.c', 'cairo_c_support.c', 'dihos-setjmp.S', 'dihos-longjmp.S',",1)
if (native/'src/meson.build').read_text()!=meson:(native/'src/meson.build').write_text(meson)
toolchain=Path.home()/'.cache/dihos-jsc-target/toolchain.cmake'
cflags=shlex.split(re.search(r'set\(CMAKE_C_FLAGS_INIT "(.*?)"\)',toolchain.read_text()).group(1))
headers=cache/'c-headers';headers.mkdir(exist_ok=True)
abi_headers=[ROOT/'sdk/sacx/user_runtime/libcxx/c'/name for name in ['math.h','stdio.h','stdlib.h','errno.h']]
for header in abi_headers:
    target=headers/header.name;data=header.read_bytes()
    if not target.exists() or target.read_bytes()!=data:target.write_bytes(data)
cflags=['-I'+str(ROOT/'tools/webcore_dihos/graphics'),'-I'+str(headers),'-I'+str(Path.home()/'.cache/dihos-jsc-target/headers-59ec4e6b498f1bb4/c-native')]+cflags
cflags+=['-O2','-ffunction-sections','-fdata-sections']
# Meson removes default system include flags, even with -nostdinc. Preserve
# Clang's verified freestanding resource headers through its internal flag.
cflags+=['-Xclang','-internal-isystem','-Xclang','/usr/lib/llvm-22/lib/clang/22/include']
backend_sources=sorted(p for p in (ROOT/'tools/webcore_dihos/graphics').iterdir() if p.suffix in ['.h','.c','.S'])
profile=hashlib.sha256(''.join(sha(p) for p in [*abi_headers,*backend_sources]).encode()).hexdigest()[:16]
cflags+=['-DDIHOS_GRAPHICS_PROFILE=0x'+profile+'ULL']
math=json.loads((ROOT/'build/user-libm/report.json').read_text());math_lib=Path(math['library']['path'])
if sha(math_lib)!=math['library']['sha256']:raise SystemExit('Changed native math archive')
stage=cache/'stage';(stage/'lib').mkdir(parents=True,exist_ok=True)
shutil.copyfile(math_lib,stage/'lib/libm.a')
jsc=json.loads((ROOT/'build/jsc-dihos/report.json').read_text())
link=next(a.split('=',1)[1] for a in next(s for s in jsc['steps'] if s['label']=='engine-configure')['argv'] if a.startswith('-DCMAKE_EXE_LINKER_FLAGS='))
link_args=shlex.split(link)+['-L'+str(stage/'lib'),str(Path.home()/'.cache/dihos-jsc-target/feature-main.o'),jsc['runtime_archive']['path']]
cflags+=['-I'+str(stage/'include')]
cross=cache/'native.ini'
cross.write_text("[binaries]\nc = 'clang-22'\ncpp = 'clang++-22'\nar = 'aarch64-linux-gnu-ar'\nstrip = 'aarch64-linux-gnu-strip'\npkg-config = 'pkg-config'\n"
    "[host_machine]\nsystem = 'dihos'\ncpu_family = 'aarch64'\ncpu = 'armv8-a'\nendian = 'little'\n"
    f"[properties]\nneeds_exe_wrapper = true\npkg_config_libdir = ['{stage}/lib/pkgconfig']\n"
    f"[built-in options]\ndefault_library = 'static'\nb_staticpic = true\nc_args = {cflags!r}\nc_link_args = {link_args!r}\n")
env=dict(os.environ,PKG_CONFIG_LIBDIR=str(stage/'lib/pkgconfig'),PKG_CONFIG_PATH='')
report.update(cross_file_sha256=sha(cross),native_sources={str(p.relative_to(ROOT)):sha(p) for p in backend_sources},steps=[],native_unwind_archive_sha256=sha(musl))
report['native_sources'].update({str(p.relative_to(ROOT)):sha(p) for p in abi_headers})
def save():(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label,command):
    with (out/(label+'.log')).open('w') as stream:code=subprocess.call(command,env=env,stdout=stream,stderr=subprocess.STDOUT)
    report['steps'].append({'label':label,'argv':command,'exit_code':code});save();print(label,code,flush=True)
    if code:raise SystemExit(code)
packages=[('pixman',pixman_source,['-Dtests=disabled','-Ddemos=disabled','-Dgtk=disabled','-Dlibpng=disabled','-Dopenmp=disabled','-Da64-neon=enabled','-Dtls=enabled']),
          ('cairo',native,['-D'+name+'=disabled' for name in ['dwrite','fontconfig','freetype','png','quartz','tee','xcb','xlib','xlib-xcb','zlib','tests','lzo','gtk2-utils','glib','spectre','symbol-lookup']])]
for name,source,options in packages:
    build=cache/(name+'-build')
    command=['meson','setup',str(build),str(source),'--cross-file',str(cross),'--prefix',str(stage),'--libdir','lib','--wrap-mode=nofallback','-Dauto_features=disabled','-Dbuildtype=release',*options]
    stamp=cache/(name+'-cross.sha256')
    stamp_value=sha(cross)+str(source)
    if (build/'meson-private/coredata.dat').exists():
        command.insert(2,'--reconfigure' if stamp.exists() and stamp.read_text()==stamp_value else '--wipe')
    run(name+'-configure',command)
    stamp.write_text(stamp_value)
    run(name+'-build',['meson','compile','-C',str(build),'-j','4'])
    run(name+'-install',['meson','install','-C',str(build),'--no-rebuild'])
report.update(libraries_built=True,libraries={str(p):sha(p) for p in [stage/'lib/libcairo.a',stage/'lib/libpixman-1.a']},stage=str(stage))
save();print('Native Cairo and Pixman archives built; execution not yet tested')
