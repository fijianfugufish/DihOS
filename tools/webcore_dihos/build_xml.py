"""Build the pinned XML parser for native WebCore, never with host libraries.

This initial library profile has no file, socket, module or threading backend.
It is a build prerequisite, not permission to load external XML entities.
"""
from pathlib import Path
import hashlib, json, os, platform, subprocess, tarfile

ROOT = Path(__file__).resolve().parents[2]
if platform.system() != 'Linux':
    raise SystemExit('Run with ARM64 WSL')
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
archive = ROOT/'third_party/mesa/subprojects/packagecache/libxml2-2.11.6.tar.xz'
pin = 'c90eee7506764abbe07bb616b82da452529609815aefef423d66ef080eb0c300'
if sha(archive) != pin:
    raise SystemExit('XML source archive hash mismatch')
cache = Path.home()/'.cache/dihos-webcore-native/xml-2.11.6'
cache.mkdir(parents=True, exist_ok=True)
source = cache/'libxml2-2.11.6'
if not source.exists():
    with tarfile.open(archive) as tar:
        tar.extractall(cache, filter='data')
with tarfile.open(archive) as tar:
    for member in tar:
        if member.isfile():
            path = cache/member.name
            if sha(path) != hashlib.sha256(tar.extractfile(member).read()).hexdigest():
                raise SystemExit('Modified XML source: '+member.name)
out = ROOT/'build/webcore-xml'; out.mkdir(exist_ok=True)
build = cache/'build'; stage = cache/'stage'
toolchain = Path.home()/'.cache/dihos-jsc-target/toolchain.cmake'
# C feature checks must resolve symbols, rather than accept any archive.
native_headers = Path.home()/'.cache/dihos-jsc-target/headers-59ec4e6b498f1bb4/c-native'
overlay = cache/'toolchain.cmake'
overlay.write_text(f'include("{toolchain}")\nset(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)\n'
                   f'string(PREPEND CMAKE_C_FLAGS_INIT "-I{native_headers} -include \'{ROOT / "tools/webcore_dihos/xml_io_dihos.h"}\' ")\n'
                   'set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS_INIT}" CACHE STRING "Native XML flags" FORCE)\n')
options = ['PROGRAMS','PYTHON','TESTS','HTTP','FTP','ICONV','ICU','LZMA','ZLIB','MODULES','THREADS','CATALOG']
command = ['cmake','-S',str(source),'-B',str(build),'-G','Ninja',
           '-DCMAKE_TOOLCHAIN_FILE='+str(overlay),'-DCMAKE_BUILD_TYPE=Release',
           '-DBUILD_SHARED_LIBS=OFF','-DCMAKE_INSTALL_PREFIX='+str(stage)]
command += ['-DLIBXML2_WITH_'+name+'=OFF' for name in options]
jsc = json.loads((ROOT/'build/jsc-dihos/report.json').read_text())
command += [a for a in next(s for s in jsc['steps'] if s['label']=='engine-configure')['argv']
            if a.startswith('-DCMAKE_EXE_LINKER_FLAGS=')]
report = {'target':'aarch64-none-elf','version':'2.11.6','archive_sha256':pin,
          'io_header_sha256':sha(ROOT/'tools/webcore_dihos/xml_io_dihos.h'),
          'builder_sha256':sha(Path(__file__)),'library_built':False,'steps':[],
          'profile':'single owner, no native file/network/module backends',
          'shipping_browser_changed':False}
for name, argv in [('configure',command),('build',['cmake','--build',str(build),'--target','LibXml2','-j','4']),
                   ('install',['cmake','--install',str(build)])]:
    log = out/(name+'.log')
    with log.open('w') as stream:
        code = subprocess.call(argv, stdout=stream, stderr=subprocess.STDOUT)
    report['steps'].append({'name':name,'argv':argv,'exit_code':code,'log_sha256':sha(log)})
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(name, code, flush=True)
    if code: raise SystemExit(code)
library = stage/'lib/libxml2.a'
report.update(library_built=True,library=str(library),library_sha256=sha(library),stage=str(stage))
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
