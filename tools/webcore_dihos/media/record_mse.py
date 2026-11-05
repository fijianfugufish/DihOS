"""Record source/compile/native evidence without claiming browser MSE playback."""
from pathlib import Path
import hashlib,json,sys
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/dihscover-sample-decoder-validation'
ENGINE=ROOT/'build/dihscover-mse-engine'
DEST=ROOT/'docs/browser-evaluation/dihscover-mse-sourcebuffer-20261104.json'
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def read(p):return json.loads(p.read_text())
def verify_inputs(report):
    for n,d in report['inputs'].items():assert sha(ROOT/n)==d,n
fixture=read(OUT/'fixture-report.json');native=read(OUT/'qemu/report.json');engine=read(ENGINE/'report.json')
assert fixture['linked'] and fixture['native_executed'] and fixture['host_sanitizers_passed'] and fixture['persistent_decoder_tested']
assert native['passed'] and native['real_arm64_el0'] and native['elf_sha256']==fixture['elf_sha256']==sha(OUT/'video.elf')
assert native['log_sha256']==sha(OUT/'qemu/run.log') and fixture['native_report_sha256']==sha(OUT/'qemu/report.json')
assert native['fixture_report_sha256']==sha(OUT/'fixture-build-report.json')
assert fixture['fixture_runner_sha256']==sha(OUT/'run-fixture.py')
verify_inputs(fixture);verify_inputs(native)
for n,d in fixture['linked_inputs'].items():assert sha(Path(n))==d,n
assert engine['sourcebuffer_compiled'] and not engine['sourcebuffer_executed'] and not engine['player_integrated'] and not engine['youtube_supported']
for n,d in engine['source_inputs'].items():assert sha(ROOT/n)==d,n
for n,d in engine['platform_files'].items():assert sha(Path(engine['source'])/n)==d,n
obj=Path(engine['build'])/'Source/WebCore/CMakeFiles/WebCore.dir/platform/dihos/SourceBufferDihOS.cpp.obj'
assert sha(obj)==engine['object_sha256']
assert sha(Path(engine['build'])/'cmakeconfig.h')==engine['config_sha256']
assert 'SACX persistent decoder: encoded tracks, fragment continuity, BGRA timing, backpressure and flush OK' in (OUT/'qemu/run.log').read_text()
assert 'PASS: persistent H264 decoding across fragments, owned config, timestamps, backpressure and flush' in (OUT/'host-execute.log').read_text()
assert sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d'
proof={'scope':'Native encoded sample bridge, compiled WebCore SourceBuffer adapter and persistent H264 decoder',
    'encoded_samples_executed_arm64_el0':True,'persistent_decoder_executed_arm64_el0':True,
    'sourcebuffer_compiled':True,'sourcebuffer_executed':False,'player_integrated':False,
    'youtube_supported':False,'new_browser_package':False,'vhd_updated':False,
    'elf_sha256':fixture['elf_sha256'],'sourcebuffer_object_sha256':engine['object_sha256'],
    'source_hashes':{**fixture['inputs'],**native['inputs'],**engine['source_inputs']},'proof_files':{}}
for n in ['tools/webcore_dihos/media/test_native.py','tools/webcore_dihos/media/record_mse.py','docs/dihscover-mse-sourcebuffer.md']:
    proof['source_hashes'][n]=sha(ROOT/n)
for p in [OUT/'fixture-report.json',OUT/'fixture-build-report.json',OUT/'qemu/report.json',OUT/'qemu/run.log',
          OUT/'host-execute.log',OUT/'run-fixture.py',OUT/'run-native.py',ENGINE/'report.json',ENGINE/'compile-sourcebuffer.log']:
    proof['proof_files'][p.relative_to(ROOT).as_posix()]=sha(p)
if '--check' in sys.argv:
    assert read(DEST)==proof,'MSE development evidence has changed'
    print('PASS: encoded sample/decoder execution, SourceBuffer compile, sources, linked objects and evidence hashes')
else:
    assert not DEST.exists(),'Use --check for existing evidence; never silently replace it'
    DEST.write_text(json.dumps(proof,indent=2)+'\n')
    print('Recorded verified native components; no browser MSE or YouTube playback claimed')
