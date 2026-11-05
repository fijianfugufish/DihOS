"""Build a pinned native libcrypto with real SACX synchronization and entropy.
Archive compilation is not proof of crypto, concurrency, or Page execution.
"""
from pathlib import Path
import argparse,ast,hashlib,json,os,platform,shlex,shutil,subprocess,tarfile
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--configure-only',action='store_true');args=parser.parse_args()
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
pin='603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a'
archive=ROOT/'build/browser-engine-arm64/openssl-3.5.9.tar.gz'
if sha(archive)!=pin:raise SystemExit('Pinned OpenSSL archive changed; run prepare_openssl.py')
cache=Path.home()/'.cache/dihos-openssl-3.5.9';cache.mkdir(exist_ok=True)
original=cache/'openssl-3.5.9'
if not original.exists():
    with tarfile.open(archive) as stream:stream.extractall(cache,filter='data')
source=cache/'native'
if not source.exists():shutil.copytree(original,source)
abi=ROOT/'tools/webcore_dihos/openssl'
profile=hashlib.sha256((sha(Path(__file__))+''.join(sha(p) for p in sorted(abi.iterdir()) if p.is_file() and p.name!='crypto_fixture.cpp')+sha(ROOT/'build/webcore-graphics/report.json')).encode()).hexdigest()[:16]
build=cache/('build-'+profile);build.mkdir(exist_ok=True)
out=ROOT/'build/webcore-openssl';out.mkdir(exist_ok=True)
report={'target':'aarch64-none-elf','version':'3.5.9','archive_sha256':pin,'builder_sha256':sha(Path(__file__)),
        'library_built':False,'crypto_tested':False,'threading_tested':False,'webcore_crypto_linked':False,
        'source':str(source),'build':str(build),'steps':[],
        'inputs':{str(p.relative_to(ROOT)):sha(p) for p in abi.iterdir() if p.is_file() and p.name!='crypto_fixture.cpp'}}
for name in ['sacx_user_syscall.h','sacx_user_random.h','sacx_user_keys.h','sacx_user_threads.h','sacx_user_sync.hpp']:
    p=ROOT/'sdk/sacx/include'/name;report['inputs'][str(p.relative_to(ROOT))]=sha(p)
def save():(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label,argv,cwd=build,**kwargs):
    with (out/(label+'.log')).open('w') as stream:
        code=subprocess.call([str(v) for v in argv],cwd=cwd,stdout=stream,stderr=subprocess.STDOUT,**kwargs)
    report['steps'].append({'label':label,'argv':[str(v) for v in argv],'exit_code':code});save();print(label,code,flush=True)
    if code:raise SystemExit(label+' failed; inspect '+str(out/(label+'.log')))
def patch(relative,before,after):
    text=(original/relative).read_text()
    if text.count(before)!=1:raise SystemExit('Upstream contract changed: '+relative)
    updated=text.replace(before,after);p=source/relative
    if p.read_text()!=updated:p.write_text(updated)
    report.setdefault('patches',{})[relative]={'before':sha(original/relative),'after':sha(p)}
patch('include/openssl/e_os2.h','#define OPENSSL_SYS_UNIX\n','#if !defined(OPENSSL_SYS_DIHOS)\n#define OPENSSL_SYS_UNIX\n#endif\n')
patch('crypto/uid.c','#if defined(OPENSSL_SYS_WIN32)',
      '''#if defined(OPENSSL_SYS_DIHOS)
/* SACX has capabilities, not Unix setuid. Always ignore environment-selected
 * providers/configuration; no authority comes from a fabricated UID. */
int OPENSSL_issetugid(void) { return 1; }
#elif defined(OPENSSL_SYS_WIN32)''')
patch('providers/implementations/rands/seeding/build.info','$COMMON=rand_unix.c rand_win.c rand_tsc.c',
      '$COMMON=rand_dihos.c rand_win.c rand_tsc.c')
sleep=(original/'crypto/sleep.c').read_text()
patch('crypto/sleep.c',sleep,'''#if defined(__DIHOS__)
#include <openssl/crypto.h>
#include <sacx_user_syscall.h>
#include <stdlib.h>
void OSSL_sleep(uint64_t millis){
    while(millis){uint64_t n=millis>60000?60000:millis;
        if(sacx_user_call(SACX_USER_SLEEP_MS,n,0,0))abort();
        millis-=n;
    }
}
#else
'''+sleep+'\n#endif\n')
patch('crypto/time.c','#if defined(_WIN32) && !defined(OPENSSL_SYS_UEFI)',
      '''#if defined(OPENSSL_SYS_DIHOS)
    int64_t millis = sacx_user_call(SACX_USER_UTC_MS, 0, 0, 0);
    if (millis < 0 || (uint64_t)millis > UINT64_MAX / OSSL_TIME_MS) {
        ERR_raise_data(ERR_LIB_SYS, 5, "DihOS UTC clock unavailable");
        return ossl_time_zero();
    }
    r.t = (uint64_t)millis * OSSL_TIME_MS;
#elif defined(_WIN32) && !defined(OPENSSL_SYS_UEFI)''')
shutil.copyfile(abi/'rand_dihos.c',source/'providers/implementations/rands/seeding/rand_dihos.c')
target=source/'Configurations/99-dihos.conf'
target.write_text('''my %targets = (
    "dihos-aarch64" => {
        inherit_from => [ "BASE_unix" ],
        CC => "clang-22", AR => "aarch64-linux-gnu-ar", RANLIB => "aarch64-linux-gnu-ranlib",
        bn_ops => "SIXTY_FOUR_BIT_LONG", thread_scheme => "pthreads",
        defines => [ "OPENSSL_SYS_DIHOS", "OPENSSL_NO_UNISTD", "NO_SYSLOG", "NO_CHMOD" ],
    },
);
''')
report['target_file_sha256']=sha(target)
graphics_path=ROOT/'build/webcore-graphics/report.json';graphics=json.loads(graphics_path.read_text())
cross=Path(graphics['cache'])/'native.ini'
if sha(cross)!=graphics['cross_file_sha256']:raise SystemExit('Changed native C ABI')
flags=ast.literal_eval(next(v.split('=',1)[1].strip() for v in cross.read_text().splitlines() if v.startswith('c_args =')))
flags=['-I'+str(abi),'-I'+str(ROOT/'sdk/sacx/include'),'-include',str(abi/'native_abi.h'),'-fno-strict-aliasing',*flags]
report['native_c_abi_report_sha256']=sha(graphics_path)
# A wrapper preserves each argument (including workspace paths with spaces)
# rather than passing a joined shell string through Configure/make.
wrapper=cache/'native-cc'
wrapper.write_text('#!/bin/sh\nexec '+shlex.join(['clang-22',*flags])+' "$@"\n');wrapper.chmod(0o755)
report['compiler_wrapper_sha256']=sha(wrapper)
environment=dict(os.environ,CC=str(wrapper))
options=['no-shared','no-module','no-dso','no-apps','no-tests','no-docs','no-sock','no-stdio','no-posix-io','no-ui-console',
         'no-engine','no-asm','no-async','no-egd','no-quic','no-comp','no-zlib','no-autoload-config','no-secure-memory','no-thread-pool',
         '--with-rand-seed=os']
run('configure',['perl',source/'Configure','dihos-aarch64',*options],env=environment)
run('headers',['make','-j','4','build_generated'],env=environment)
base=json.loads((ROOT/'build/user-jsc-native/resource-report.json').read_text())
command=list(next(s for s in base['steps'] if s['label']=='compile-embedding')['argv'])
command[1:1]=['-I'+str(abi),'-fno-strict-aliasing']
command[command.index('-c')+1]=str(abi/'thread_bridge.cpp');command[command.index('-o')+1]=str(build/'thread-bridge.o')
run('thread-bridge',command)
if args.configure_only:save();raise SystemExit(0)
# The public target runs OpenSSL's dependency regeneration. Calling
# libcrypto.a directly bypasses it and can retain objects from older headers.
run('build',['make','-j','6','build_libs','LIBS=libcrypto.a'],env=environment)
library=build/'libcrypto.a'
run('archive-native-threads',['aarch64-linux-gnu-ar','rcs',library,build/'thread-bridge.o'])
report.update(library_built=True,library=str(library),library_sha256=sha(library),includes=[str(build/'include'),str(source/'include')]);save()
print('Native OpenSSL archive built; crypto/concurrency/entropy validation still required',flush=True)
