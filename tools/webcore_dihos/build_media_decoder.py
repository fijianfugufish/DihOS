"""Build a pinned, memory-input-only software video decoder for DihOS ARM64.

This does not enable WebCore VIDEO or claim YouTube compatibility. No network,
file protocols, devices, encoders, GPL components, or host libc enter the target.
"""
from pathlib import Path
import ast, hashlib, json, os, platform, re, shlex, subprocess, tarfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/dihscover-media-validation'
VERSION = '9.0.2'
PIN = '8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e'
SIGNER = 'FCF986EA15E6E293A5644F10B4322F04D67658D8'
if platform.system() != 'Linux':
    raise SystemExit('Run in ARM64 WSL')
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
archive = OUT / ('ffmpeg-' + VERSION + '.tar.xz')
if sha(archive) != PIN:
    raise SystemExit('FFmpeg archive digest mismatch')
signature = subprocess.run(['gpg', '--homedir', str(OUT/'gnupg'), '--status-fd', '1',
                            '--verify', str(archive)+'.asc', str(archive)],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
(OUT/'signature.log').write_text(signature.stdout)
if signature.returncode or '[GNUPG:] VALIDSIG '+SIGNER+' ' not in signature.stdout:
    raise SystemExit('FFmpeg signature verification failed')
cache = Path.home()/'.cache'/('dihos-ffmpeg-'+VERSION)
cache.mkdir(exist_ok=True)
source = cache/('ffmpeg-'+VERSION)
if not source.exists():
    with tarfile.open(archive) as stream:
        stream.extractall(cache, filter='data')
tree=hashlib.sha256()
with tarfile.open(archive) as stream:
    for member in stream:
        if not member.isfile():continue
        expected=hashlib.sha256(stream.extractfile(member).read()).hexdigest()
        path=cache/member.name
        if not path.resolve().is_relative_to(source.resolve()) or sha(path)!=expected:
            raise SystemExit('Changed extracted FFmpeg source: '+member.name)
        tree.update((member.name+' '+expected+'\n').encode())
graphics_path = ROOT/'build/webcore-graphics/report.json'
graphics = json.loads(graphics_path.read_text())
cross = Path(graphics['cache'])/'native.ini'
if sha(cross) != graphics['cross_file_sha256']:
    raise SystemExit('Changed native C ABI configuration')
flags = ast.literal_eval(next(line.split('=',1)[1].strip()
                             for line in cross.read_text().splitlines() if line.startswith('c_args =')))
flags = ['-I'+str(ROOT/'tools/webcore_dihos/media'), *flags, '-Dstatic_assert=_Static_assert',
         '-include', str(ROOT/'tools/webcore_dihos/media/native_abi.h')]
abi_inputs={str(p.relative_to(ROOT)):sha(p) for p in sorted((ROOT/'tools/webcore_dihos/media').glob('*.h'))}
profile = hashlib.sha256((json.dumps(flags)+sha(cross)+json.dumps(abi_inputs,sort_keys=True)).encode()).hexdigest()[:16]
build = cache/('build-'+profile)
build.mkdir(exist_ok=True)
wrapper = build/'native-cc'
base = json.loads((ROOT/'build/user-jsc-native/resource-report.json').read_text())
link_template = next(s['argv'] for s in base['steps'] if s['label']=='link')
libraries = [a for a in link_template if a.endswith('.a')]
link_flags = ['-nostdlib', '--ld-path=/usr/bin/ld.lld-22', '-Wl,-e,main,--gc-sections',
              '-Wl,--start-group', *libraries, '-Wl,--end-group']
wrapper.write_text('#!/bin/sh\nfor arg do\ncase "$arg" in -c|-E|-S) exec '+
                  shlex.join(['clang-22',*flags])+' "$@" ;; esac\ndone\nexec '+
                  shlex.join(['clang-22',*flags])+' "$@" '+shlex.join(link_flags)+'\n')
wrapper.chmod(0o755)
report = {'version': VERSION, 'target':'aarch64-none-elf', 'archive_sha256':PIN,
          'signer':SIGNER, 'builder_sha256':sha(Path(__file__)), 'source':str(source),
          'build':str(build), 'compiler_flags':flags, 'libraries_built':False,
          'abi_inputs':abi_inputs,'extracted_source_sha256':tree.hexdigest(),
          'configure_libraries':{a:sha(Path(a)) for a in libraries},
          'playback_tested':False, 'browser_media_enabled':False, 'steps':[]}
def save():
    (OUT/'decoder-report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label, argv):
    with (OUT/(label+'.log')).open('w') as log:
        code = subprocess.call(list(map(str,argv)), cwd=build, stdout=log, stderr=subprocess.STDOUT)
    report['steps'].append({'label':label,'argv':list(map(str,argv)),'exit_code':code})
    save()
    print(label, code, flush=True)
    if code: raise SystemExit('Inspect '+str(OUT/(label+'.log')))
options = [
    '--enable-cross-compile','--target-os=none','--arch=aarch64','--cpu=generic',
    '--cc='+str(wrapper),'--ar=aarch64-linux-gnu-ar','--ranlib=aarch64-linux-gnu-ranlib',
    '--nm=aarch64-linux-gnu-nm','--strip=aarch64-linux-gnu-strip',
    '--disable-autodetect','--disable-everything','--disable-programs','--disable-doc',
    '--disable-network','--disable-protocols','--disable-avdevice','--disable-avfilter',
    '--disable-swresample','--disable-pthreads','--disable-w32threads','--disable-os2threads',
    '--disable-asm','--disable-inline-asm','--disable-runtime-cpudetect',
    '--disable-shared','--enable-static','--enable-pic','--disable-debug',
    '--enable-avcodec','--enable-avformat','--enable-swscale',
    '--enable-decoder=h264,vp8,vp9,mpeg4', '--enable-parser=h264,vp8,vp9,mpeg4video',
    '--enable-demuxer=mov,matroska', '--extra-ldflags=-nostdlib',
]
run('decoder-configure',[source/'configure',*options])
defines={name:int(value) for name,value in re.findall(r'^#define ([A-Z][A-Z0-9_]*) ([01])$',
    (build/'config.h').read_text()+'\n'+(build/'config_components.h').read_text(),re.M)}
required={'CONFIG_NETWORK':0,'CONFIG_GPL':0,'CONFIG_NONFREE':0,
    'CONFIG_FILE_PROTOCOL':0,'CONFIG_HTTP_PROTOCOL':0,'CONFIG_HTTPS_PROTOCOL':0,
    'CONFIG_H264_DECODER':1,'CONFIG_VP8_DECODER':1,'CONFIG_VP9_DECODER':1,
    'CONFIG_MOV_DEMUXER':1,'CONFIG_MATROSKA_DEMUXER':1}
for name,value in required.items():
    if defines.get(name)!=value:raise SystemExit('Unexpected decoder capability: '+name)
report['verified_capabilities']=required
run('decoder-build',['make','-j','6','libavformat/libavformat.a','libavcodec/libavcodec.a',
                     'libswscale/libswscale.a','libavutil/libavutil.a'])
report['libraries']={str(build/name/(name+'.a')):sha(build/name/(name+'.a'))
                     for name in ['libavformat','libavcodec','libswscale','libavutil']}
report.update(libraries_built=True, config_sha256=sha(build/'config.h'))
save()
print('Native decoder libraries built; execution and browser integration still required',flush=True)
