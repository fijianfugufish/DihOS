"""Package verified little-endian ICU data as a target object, never a stub."""
from pathlib import Path
import hashlib,json,subprocess,tarfile
ROOT=Path(__file__).resolve().parents[1]
digest='3e99687b5c435d4b209630e2d2ebb79906c984685e78635078b672e03c89df35'
archive=ROOT/'build/browser-engine-arm64/icu4c-78.2-sources.tgz'
if hashlib.sha256(archive.read_bytes()).hexdigest()!=digest:raise SystemExit('Wrong ICU archive')
out=ROOT/'build/icu-dihos';out.mkdir(exist_ok=True)
with tarfile.open(archive) as tar:
    data=tar.extractfile('icu/source/data/in/icudt78l.dat').read()
if data[2:4]!=b'\xda\x27' or data[8:11]!=b'\0\0\2' or data[12:16]!=b'CmnD':
    raise SystemExit('Expected little-endian ASCII-family ICU common data with 16-bit UChar')
binary=out/'icudt78l.dat';binary.write_bytes(data)
assembly=out/'icudata.S'
assembly.write_text('.section .rodata.icudata,"a"\n.balign 16\n.global icudt78_dat\n.type icudt78_dat,%object\nicudt78_dat:\n.incbin '+json.dumps(str(binary))+'\n.size icudt78_dat, .-icudt78_dat\n.section .note.GNU-stack,"",%progbits\n')
obj=out/'icudata.o';library=out/'libicudata.a'
subprocess.run(['clang-22','--target=aarch64-none-elf','-c',str(assembly),'-o',str(obj)],check=True)
subprocess.run(['aarch64-linux-gnu-ar','rcs',str(library),str(obj)],check=True)
undefined=subprocess.check_output(['aarch64-linux-gnu-nm','-u',str(obj)],text=True)
if undefined.strip():raise SystemExit('Unexpected data imports')
result={'version':'78.2','archive_sha256':digest,'data_bytes':len(data),
        'data_sha256':hashlib.sha256(data).hexdigest(),'library':str(library),
        'library_sha256':hashlib.sha256(library.read_bytes()).hexdigest(),
        'symbol':'icudt78_dat','alignment':16,'guest_validated':False}
(out/'data-report.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
