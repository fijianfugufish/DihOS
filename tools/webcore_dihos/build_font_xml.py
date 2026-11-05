"""Native-C-ABI XML parser for memory-only Fontconfig rules."""
from pathlib import Path
import ast,hashlib,json,platform,shlex,subprocess
ROOT=Path(__file__).resolve().parents[2]
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
graphics_path=ROOT/'build/webcore-graphics/report.json';graphics=json.loads(graphics_path.read_text())
cross=Path(graphics['cache'])/'native.ini'
if sha(cross)!=graphics['cross_file_sha256']:raise SystemExit('Changed native C ABI')
flags=ast.literal_eval(next(line.split('=',1)[1].strip() for line in cross.read_text().splitlines() if line.startswith('c_args =')))
flags+=['-include',str(ROOT/'tools/webcore_dihos/xml_io_dihos.h')]
old=json.loads((ROOT/'build/webcore-xml/report.json').read_text())
archive=ROOT/'third_party/mesa/subprojects/packagecache/libxml2-2.11.6.tar.xz'
if sha(archive)!=old['archive_sha256']:raise SystemExit('Changed pinned XML source')
import tarfile
cache=Path.home()/'.cache/dihos-webcore-native/font-xml-2.11.6';cache.mkdir(exist_ok=True)
source=cache/'libxml2-2.11.6'
if not source.exists():
    with tarfile.open(archive) as t:t.extractall(cache,filter='data')
with tarfile.open(archive) as t:
    for member in t:
        if member.isfile() and sha(cache/member.name)!=hashlib.sha256(t.extractfile(member).read()).hexdigest():raise SystemExit('Modified XML source')
toolchain=cache/'toolchain.cmake';original=Path.home()/'.cache/dihos-jsc-target/toolchain.cmake'
toolchain.write_text(f'include("{original}")\nset(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)\nset(CMAKE_C_FLAGS "{shlex.join(flags)}" CACHE STRING "Native XML C ABI" FORCE)\n')
out=ROOT/'build/webcore-font-xml';out.mkdir(exist_ok=True);build=cache/'build';stage=cache/'stage'
command=['cmake','-S',source,'-B',build,'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(toolchain),'-DCMAKE_BUILD_TYPE=Release','-DBUILD_SHARED_LIBS=OFF','-DCMAKE_INSTALL_PREFIX='+str(stage)]
command+=['-DLIBXML2_WITH_'+option+'=OFF' for option in ['PROGRAMS','PYTHON','TESTS','HTTP','FTP','ICONV','ICU','LZMA','ZLIB','MODULES','THREADS','CATALOG','HTML','XPATH','XPTR','XPTR_LOCS','C14N','DEBUG','SCHEMAS','SCHEMATRON','XINCLUDE','VALID','READER','WRITER']]
jsc=json.loads((ROOT/'build/jsc-dihos/report.json').read_text())
command += [a for a in next(s for s in jsc['steps'] if s['label']=='engine-configure')['argv'] if a.startswith('-DCMAKE_EXE_LINKER_FLAGS=')]
report={'target':'aarch64-none-elf','version':'2.11.6','builder_sha256':sha(Path(__file__)),'archive_sha256':sha(archive),'library_built':False,'graphics_report_sha256':sha(graphics_path),'io_header_sha256':sha(ROOT/'tools/webcore_dihos/xml_io_dihos.h'),'steps':[]}
for label,argv in [('configure',command),('build',['cmake','--build',build,'--target','LibXml2','-j','4']),('install',['cmake','--install',build])]:
    with (out/(label+'.log')).open('w') as stream:code=subprocess.call([str(x) for x in argv],stdout=stream,stderr=subprocess.STDOUT,cwd=ROOT)
    report['steps'].append({'label':label,'argv':[str(x) for x in argv],'exit_code':code});(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(label,code,flush=True)
    if code:raise SystemExit(label+' failed')
library=stage/'lib/libxml2.a';report.update(library_built=True,library=str(library),library_sha256=sha(library),stage=str(stage))
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
