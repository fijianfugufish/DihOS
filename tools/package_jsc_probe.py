"""Stage the proven standalone engine and source recipe in an isolated image tree."""
from pathlib import Path
import argparse,hashlib,json,shutil,tarfile,zipfile

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('project',type=Path);parser.add_argument('--extended',action='store_true');parser.add_argument('--desktop',action='store_true');parser.add_argument('--sparse-adapter',action='store_true');args=parser.parse_args()
if args.sparse_adapter:args.desktop=True
if args.desktop:args.extended=True
project=args.project.resolve()
if not project.is_relative_to(ROOT/'build') or not (project.parent/'source.json').is_file():
    raise SystemExit('Use an isolated exported source snapshot beneath build/')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
proof_path=ROOT/('docs/browser-evaluation/native-javascriptcore-sparse-adapter-20261031.json' if args.sparse_adapter else
    'docs/browser-evaluation/native-javascriptcore-desktop-20261030.json' if args.desktop else
    'docs/browser-evaluation/native-javascriptcore-suite-20261030.json' if args.extended
    else 'docs/browser-evaluation/native-javascriptcore-20261030.json')
proof=json.loads(proof_path.read_text());report_path=ROOT/'build/user-jsc-native/report.json'
report=json.loads(report_path.read_text())
if not proof['qemu_validated'] or sha(report_path)!=proof['build_report_sha256']:
    raise SystemExit('Record a passing test for this exact native engine first')
for relative,digest in proof['source_hashes'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Changed tested source: '+relative)
    if args.desktop and relative.startswith('kernel/') and sha(project/relative)!=digest:
        raise SystemExit('Isolated kernel source does not match desktop test: '+relative)
key=Path(report['output']).name
if len(key)!=12 or any(c not in '0123456789abcdef' for c in key):raise SystemExit('Unexpected engine path')
engine=ROOT/'build/user-jsc-native'/key/'jsc.elf'
if sha(engine)!=proof['engine_elf_sha256']:raise SystemExit('Engine ELF changed')
destination=project/'OS/System/Programs/UserRuntime';destination.mkdir(parents=True,exist_ok=True)
shutil.copy2(engine,destination/'jsc.elf')
spin_path=None
if args.extended:
    spin_path=ROOT/'build/user-jsc-native/spin-report.json';spin=json.loads(spin_path.read_text())
    if not proof.get('cancellation_cleanup_verified') or sha(spin_path)!=proof['spin_build_report_sha256']:
        raise SystemExit('Record a passing cancellation test for this exact busy engine first')
    spin_key=Path(spin['output']).name
    if len(spin_key)!=12 or any(c not in '0123456789abcdef' for c in spin_key):raise SystemExit('Unexpected spin artifact path')
    spin_engine=ROOT/'build/user-jsc-native'/spin_key/'jsc.elf'
    if sha(spin_engine)!=proof['spin_elf_sha256']:raise SystemExit('Busy engine ELF changed')
    shutil.copy2(spin_engine,destination/'jsc_spin.elf')
licenses=destination/'Licenses';licenses.mkdir(exist_ok=True)
archives={
 'build/browser-engine-arm64/wpewebkit-2.54.2.tar.xz':('f3284e19e47fcb5ac8c63c2e1c113dbd65b2423eb8a545f5b345707829004f56',{
 'wpewebkit-2.54.2/Source/JavaScriptCore/COPYING.LIB':'JavaScriptCore.txt'}),
 'build/browser-engine-arm64/icu4c-78.2-sources.tgz':('3e99687b5c435d4b209630e2d2ebb79906c984685e78635078b672e03c89df35',{'icu/LICENSE':'ICU.txt'}),
 'build/musl-1.2.5.tar.gz':('a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4',{'musl-1.2.5/COPYRIGHT':'musl.txt'})}
for relative,(digest,members) in archives.items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Pinned source archive changed: '+relative)
    found=set()
    with tarfile.open(ROOT/relative,'r|*') as archive:
        for member in archive:
            if member.name in members:
                (licenses/members[member.name]).write_bytes(archive.extractfile(member).read());found.add(member.name)
    if found!=set(members):raise SystemExit('Missing upstream licence in '+relative)
for source,name in [('build/user-libcxx-22.1.0/libcxx/LICENSE.TXT','LLVM-libcxx.txt'),
                    ('build/user-builtins-22.1.0/compiler-rt/LICENSE.TXT','LLVM-compiler-rt.txt'),
                    ('third_party/tlsf/tlsf.h','TLSF.txt')]:
    shutil.copy2(ROOT/source,licenses/name)
# Keep matching pinned upstream sources and the native patches/build recipes
# with the development image, rather than depending on a host-installed SDK.
source_zip=destination/'JSC-SOURCES.zip'
files=set(ROOT/p for p in archives)
for directory in ['tools/jsc_dihos','sdk/sacx/user_runtime','sdk/sacx/include',
                  'sdk/sacx/freestanding/include','sdk/sacx/apps/jsc_probe',
                  'build/user-libcxx-22.1.0','build/user-builtins-22.1.0']:
    files.update(p for p in (ROOT/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
files.update(p for p in (ROOT/'tools').iterdir() if p.is_file() and p.suffix in ('.py','.ps1','.cpp','.c','.h','.ld','.json','.inc'))
files.update([ROOT/'third_party/tlsf/tlsf.c',ROOT/'third_party/tlsf/tlsf.h',report_path,proof_path])
if spin_path:files.add(spin_path)
files.update([ROOT/'kernel/process/dihos_user_elf.c',ROOT/'kernel/include/process/dihos_user_elf.h'])
with zipfile.ZipFile(source_zip,'w',zipfile.ZIP_DEFLATED,compresslevel=1) as archive:
    written=set()
    for path in sorted(files):
        relative=path.relative_to(ROOT).as_posix();archive.write(path,relative);written.add(relative)
    overlay_path=project.parent/'source.json';overlay=json.loads(overlay_path.read_text())
    archive.write(overlay_path,'DIHOS-KERNEL-OVERLAY.json')
    for relative,digest in overlay['files'].items():
        source=(project/relative).resolve()
        if not source.is_relative_to(project) or sha(source)!=digest:
            raise SystemExit('Isolated source overlay changed: '+relative)
        if relative not in written:archive.write(source,relative);written.add(relative)
(destination/'JSC-TEST.txt').write_text('Standalone JavaScriptCore development test; does not change Dihscover.\n'
 'proc:run 0:/OS/System/Programs/UserRuntime/jsc.elf\nproc:status\n'
 'Expected: exited, status 0x57, SACX JavaScriptCore: native script evaluation OK.\n'
 'Admission reads about 49 MiB incrementally. Check proc:status again while loading.\n'
 'proc:stop cancels the test. Hyper-V execution of this build is not yet validated.\n'
 + ('Also test proc:run 0:/OS/System/Programs/UserRuntime/jsc_spin.elf\n'
    'Use the desktop while it runs, check increasing preemptions with proc:status, then proc:stop.\n' if args.extended else '')+
 'Matching upstream sources, native patches and recipes: JSC-SOURCES.zip.\n')
record={'engine_sha256':sha(engine),'proof_sha256':sha(proof_path),'source_zip_sha256':sha(source_zip),
        'suite_revision':4 if args.sparse_adapter else (3 if args.desktop else (2 if args.extended else 1)),
        'upstream_archives':{path:entry[0] for path,entry in archives.items()}}
if args.extended:record['spin_engine_sha256']=sha(destination/'jsc_spin.elf')
(destination/'JSC-SOURCE.JSON').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps({'project':str(project),'engine_sha256':record['engine_sha256'],'source_zip_bytes':source_zip.stat().st_size}))
