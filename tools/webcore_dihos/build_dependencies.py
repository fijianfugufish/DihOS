"""Compile pinned in-memory zlib and no-filesystem SQLite for native WebCore.

No POSIX VFS is registered. Persistent SQLite databases remain unavailable;
this experimental profile is not the shipping browser's storage backend.
"""
from pathlib import Path
import hashlib,json,platform,re,shlex,subprocess,tarfile,zipfile
ROOT=Path(__file__).resolve().parents[2]
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
archives=ROOT/'build/browser-engine-arm64'
sqlite=archives/'sqlite-amalgamation-3540100.zip';zlib=archives/'zlib-1.3.2.tar.gz'
if hashlib.sha3_256(sqlite.read_bytes()).hexdigest()!='92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30':
    raise SystemExit('SQLite official SHA3-256 mismatch')
if sha(zlib)!='bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16':
    raise SystemExit('zlib official SHA-256 mismatch')
cache=Path.home()/'.cache/dihos-webcore-native/dependencies';cache.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(sqlite) as archive:
    for item in archive.infolist():
        if not item.is_dir():
            path=cache/item.filename
            if not path.resolve().is_relative_to(cache.resolve()):raise SystemExit('Unsafe archive path')
            data=archive.read(item)
            if path.exists() and path.read_bytes()!=data:raise SystemExit('Modified SQLite source')
            path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
with tarfile.open(zlib) as archive:
    archive.extractall(cache,filter='data')
z=cache/'zlib-1.3.2';s=cache/'sqlite-amalgamation-3540100'
# zutil only includes gzguts for the platform file declarations. This build
# excludes every gz* file unit but retains upstream's real calloc/free defaults.
zutil=(z/'zutil.c').read_text()
if zutil.count('#  include "gzguts.h"')!=1:raise SystemExit('Unexpected zutil include')
zutil_memory=cache/'zutil-memory.c'
zutil_memory.write_text(zutil.replace('#  include "gzguts.h"','/* No gz file API in this memory-only build. */'))
toolchain=Path.home()/'.cache/dihos-jsc-target/toolchain.cmake'
flags=shlex.split(re.search(r'set\(CMAKE_C_FLAGS_INIT "(.*?)"\)',toolchain.read_text()).group(1))
flags=['-I'+str(Path.home()/'.cache/dihos-jsc-target/headers-59ec4e6b498f1bb4/c-native')]+flags
flags+=['-O2','-DNDEBUG','-ffunction-sections','-fdata-sections']
out=ROOT/'build/webcore-dependencies';out.mkdir(exist_ok=True)
report={'target':'aarch64-none-elf','builder_sha256':sha(Path(__file__)),
        'archives':{str(p.relative_to(ROOT)):sha(p) for p in [sqlite,zlib]},
        'libraries':{},'steps':[],'libraries_built':False,'shipping_browser_changed':False}
sqlite_units=[s/'sqlite3.c',ROOT/'tools/webcore_dihos/sqlite_os_dihos.c']
z_units=[z/(name+'.c') for name in ['adler32','compress','crc32','deflate','infback','inffast','inflate','inftrees','trees','uncompr']]+[zutil_memory]
for name,units,defs in [('sqlite3',sqlite_units,['SQLITE_OS_OTHER=1','SQLITE_THREADSAFE=0','SQLITE_OMIT_LOAD_EXTENSION','SQLITE_OMIT_LOCALTIME','SQLITE_OMIT_WAL','INFINITY=__builtin_inff()']),
                        ('z',z_units,[])]:
    objects=[];log=out/(name+'.log')
    with log.open('w') as stream:
        for unit in units:
            obj=cache/(name+'-'+unit.stem+'.o');objects.append(obj)
            argv=['clang-22',*flags,*['-D'+x for x in defs],'-I'+str(s),'-I'+str(z),'-c',str(unit),'-o',str(obj)]
            code=subprocess.call(argv,stdout=stream,stderr=subprocess.STDOUT)
            report['steps'].append({'source':str(unit),'source_sha256':sha(unit),'argv':argv,'exit_code':code})
            if code:
                (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');raise SystemExit(code)
    library=cache/('lib'+name+'.a')
    subprocess.run(['aarch64-linux-gnu-ar','rcs',str(library),*[str(o) for o in objects]],check=True)
    report['libraries'][name]={'path':str(library),'sha256':sha(library),'include':str(s if name=='sqlite3' else z)}
report['libraries_built']=True
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print('Native SQLite and zlib archives built',flush=True)
