"""Seal exact decoder evidence; never label decode tests as browser playback."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/dihscover-media-validation'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
decoder=json.loads((OUT/'decoder-report.json').read_text())
fixture=json.loads((OUT/'fixture-report.json').read_text())
native=json.loads((OUT/'qemu/report.json').read_text())
reference=json.loads((OUT/'reference-report.json').read_text())
assert decoder['libraries_built'] and decoder['verified_capabilities']['CONFIG_NETWORK']==0
assert decoder['builder_sha256']==sha(ROOT/'tools/webcore_dihos/build_media_decoder.py')
for name,digest in decoder['abi_inputs'].items():assert sha(ROOT/name)==digest,name
for name,digest in decoder['libraries'].items():assert sha(Path(name))==digest,name
assert fixture['decoder_report_sha256']==sha(OUT/'decoder-report.json')
assert fixture['host_sanitizers_passed'] and fixture['linked']
for name,digest in fixture['inputs'].items():assert sha(ROOT/name)==digest,name
for name,digest in fixture['linked_inputs'].items():assert sha(Path(name))==digest,name
assert native['passed'] and native['real_arm64_el0']
assert native['fixture_report_sha256']==sha(OUT/'fixture-report.json')
assert native['elf_sha256']==fixture['elf_sha256']==sha(OUT/'video.elf')
for name,digest in native['inputs'].items():assert sha(ROOT/name)==digest,name
assert native['log_sha256']==sha(OUT/'qemu/run.log')
assert reference['fixture_sha256']==sha(OUT/'red-blue.mp4')
assert reference['hd_fixture_sha256']==sha(OUT/'red-blue-1080.mp4')
log=(OUT/'qemu/run.log').read_text()
assert 'PASS: native EL0 MP4/H.264 decoder' in log and 'FAIL:' not in log
kernel=ROOT/'OS/aa64/KERNEL.ELF'
assert sha(kernel)=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d'
files=[OUT/'decoder-report.json',OUT/'fixture-report.json',OUT/'reference-report.json',
    OUT/'signature.log',OUT/'qemu/report.json',OUT/'qemu/run.log',OUT/'host-execute.log',
    OUT/'video.elf',OUT/'red-blue.mp4',OUT/'red-blue-1080.mp4']
sources=[ROOT/'tools/webcore_dihos/build_media_decoder.py',ROOT/'tools/user_runtime_qemu_tests.c',
         *[p for p in sorted((ROOT/'tools/webcore_dihos/media').iterdir()) if p.is_file()],
         ROOT/'docs/dihscover-native-video.md']
report={'milestone':'native media decoder foundation','native_arm64_decode_tested':True,
    'hyperv_playback_tested':False,'browser_video_enabled':False,'youtube_playback_tested':False,
    'codecs_built':['H.264','VP8','VP9','MPEG-4'],'codecs_tested':['H.264 baseline I_PCM'],
    'checks':['owned memory input','MP4 sample tables and AVC configuration','BGRA pixels',
              'presentation timestamps','seek after EOF and back to start','early close',
              'truncated headers','1920x1080 SPS crop','complete process page cleanup'],
    'files':{str(p.relative_to(ROOT)):sha(p) for p in files},
    'sources':{str(p.relative_to(ROOT)):sha(p) for p in sources},
    'kernel_unchanged_sha256':sha(kernel),'native_groups':len(native['groups']),
    'remaining':['WebCore video-enabled engine build and media player integration',
                 'range-backed progressive input and MediaSource/SourceBuffer',
                 'YouTube bootstrap compatibility and actual muted playback testing']}
path=ROOT/'docs/browser-evaluation/dihscover-native-video-decoder-20261103.json'
path.write_text(json.dumps(report,indent=2)+'\n')
print('Recorded native decoder evidence; browser video and YouTube playback remain unimplemented',flush=True)
