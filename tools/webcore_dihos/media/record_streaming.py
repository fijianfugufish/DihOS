"""Record/check the executed streaming foundation without claiming MSE or YouTube."""
from pathlib import Path
import hashlib,json,sys
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/dihscover-streaming-validation'
DEST=ROOT/'docs/browser-evaluation/dihscover-streaming-video-20261104.json'
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def read(p):return json.loads(p.read_text())
def verified_inputs(report):
    for n,d in report['inputs'].items():assert sha(ROOT/n)==d,n
fixture=read(OUT/'fixture-report.json');native=read(OUT/'qemu/report.json')
assert fixture['linked'] and fixture['native_executed'] and fixture['host_sanitizers_passed']
assert native['passed'] and native['real_arm64_el0']
assert native['elf_sha256']==fixture['elf_sha256']==sha(OUT/'video.elf')
assert native['log_sha256']==sha(OUT/'qemu/run.log')
assert fixture['native_report_sha256']==sha(OUT/'qemu/report.json')
verified_inputs(fixture);verified_inputs(native)
for n,d in fixture['linked_inputs'].items():assert sha(Path(n))==d,n
expected='SACX fMP4: split appends, fragment decoding, timestamps, backpressure, abort and malformed input OK'
assert expected in (OUT/'qemu/run.log').read_text()
assert 'PASS: streaming fMP4 assembly and real H.264 fragment decoding' in (OUT/'host-execute.log').read_text()
proof={'scope':'Incremental fragmented MP4 assembly and native H.264 fragment decoding',
       'passed':True,'real_arm64_el0':True,'host_wrapper_sanitizers':['address','undefined'],
       'sourcebuffer_integrated':False,'youtube_playback_supported':False,'vhd_updated':False,
       'segment_bytes_limit':8388608,'initialization_bytes_limit':1048576,
       'elf_sha256':fixture['elf_sha256'],
       'source_hashes':{**fixture['inputs'],**native['inputs']},
       'proof_files':{}}
for name in ['tools/webcore_dihos/media/test_native.py','tools/webcore_dihos/media/record_streaming.py',
             'docs/dihscover-streaming-video.md']:
    proof['source_hashes'][name]=sha(ROOT/name)
for p in [OUT/'fixture-report.json',OUT/'qemu/report.json',OUT/'qemu/run.log',OUT/'host-execute.log',
          OUT/'run-native.py',OUT/'fragmented.mp4']:
    proof['proof_files'][p.relative_to(ROOT).as_posix()]=sha(p)
if '--check' in sys.argv:
    assert read(DEST)==proof,'Recorded streaming proof is stale'
    print('PASS: streaming sources, fixture, linked inputs, ARM64 execution and evidence hashes')
else:
    assert not DEST.exists(),'Use --check for existing evidence; do not silently replace it'
    DEST.write_text(json.dumps(proof,indent=2)+'\n')
    print('Recorded native streaming foundation; MSE and YouTube remain unsupported')
