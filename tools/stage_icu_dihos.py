"""Stage verified target ICU archives/headers in the experimental sysroot.

Archives may still depend on unresolved C/WTF runtime services. This is for
cross-build development, not evidence of a final engine link or guest install.
"""
from pathlib import Path
import hashlib,json,shutil,tarfile
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
icu=json.loads((ROOT/'build/icu-dihos/report.json').read_text())
data=json.loads((ROOT/'build/icu-dihos/data-report.json').read_text())
if not icu['all_sources'] or not icu['libraries_built'] or icu['failed']:raise SystemExit('ICU source archive build incomplete')
for relative,digest in icu['sdk_inputs'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('SDK changed: '+relative)
if sha(ROOT/'tools/jsc_dihos/icu-platform.h')!=icu['profile_sha256']:raise SystemExit('ICU profile changed')
if sha(ROOT/'tools/jsc_dihos/icu-locale.h')!=icu['locale_header_sha256']:raise SystemExit('ICU locale input changed')
sysroot=Path(icu['runtime_archive']['path']).parents[1]/'sysroot'
include=sysroot/'usr/include/unicode';lib=sysroot/'usr/lib'
include.mkdir(parents=True,exist_ok=True);lib.mkdir(parents=True,exist_ok=True)
files={}
libraries=[(Path(record['path']),record['sha256']) for record in icu['archives'].values()]
libraries.append((Path(data['library']),data['library_sha256']))
for library,digest in libraries:
    if sha(library)!=digest:raise SystemExit('Modified ICU library')
    target=lib/library.name;shutil.copyfile(library,target);files[str(target.relative_to(sysroot))]=sha(target)
archive=ROOT/'build/browser-engine-arm64'/icu['pin']['archive']
if sha(archive)!=icu['pin']['sha256'] or data['archive_sha256']!=icu['pin']['sha256']:raise SystemExit('ICU data/source pin mismatch')
with tarfile.open(archive) as tar:
    for member in tar:
        if member.isfile() and any(member.name.startswith('icu/source/'+component+'/unicode/') for component in ['common','i18n']) and member.name.endswith('.h'):
            target=include/Path(member.name).name;contents=tar.extractfile(member).read()
            relative=str(target.relative_to(sysroot))
            if relative in files and files[relative]!=hashlib.sha256(contents).hexdigest():raise SystemExit('Conflicting Unicode header')
            target.write_bytes(contents);files[relative]=sha(target)
    licence=sysroot/'usr/share/licenses/ICU/LICENSE';licence.parent.mkdir(parents=True,exist_ok=True)
    licence.write_bytes(tar.extractfile('icu/LICENSE').read());files[str(licence.relative_to(sysroot))]=sha(licence)
result={'sysroot':str(sysroot),'version':icu['pin']['version'],'files':files,
        'profile_sha256':icu['profile_sha256'],'source_archive_sha256':icu['pin']['sha256'],
        'icu_report_sha256':sha(ROOT/'build/icu-dihos/report.json'),
        'final_link_validated':False,'guest_validated':False}
(ROOT/'build/icu-dihos/stage-report.json').write_text(json.dumps(result,indent=2));print(json.dumps({'sysroot':str(sysroot),'files':len(files)}))
