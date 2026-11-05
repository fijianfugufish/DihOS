"""Validate the actual encoded-sample bridge without touching sealed artifacts."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[3]
MEDIA=ROOT/'tools/webcore_dihos/media'
decoder='--decoder' in sys.argv
output='dihscover-sample-decoder-validation' if decoder else 'dihscover-encoded-validation'
OUT=ROOT/'build'/output;OUT.mkdir(exist_ok=True)
# Keep the same verified compiler/link recipes and native EL0 harness. The
# generated runner is captured in the evidence rather than modifying old tests.
template=(MEDIA/'test_fragments.py').read_text()
assert template.count("OUT=ROOT/'build/dihscover-streaming-validation'")==1
template=template.replace('ROOT=Path(__file__).resolve().parents[3]','ROOT=Path('+repr(str(ROOT))+')',1)
template=template.replace("OUT=ROOT/'build/dihscover-streaming-validation'","OUT=ROOT/'build/"+output+"'")
template=template.replace('from make_fragment_fixture import fragmented','sys.path.insert(0,'+repr(str(MEDIA))+')\nfrom make_fragment_fixture import fragmented')
template=template.replace("['fragmented_mp4','memory_video']","['fragmented_mp4','encoded_media']")
template=template.replace("media/'fragment_fixture.cpp'","media/'"+('sample_decoder_fixture.cpp' if decoder else 'encoded_fixture.cpp')+"'")
template=template.replace("OUT/'memory_video.host.o'","OUT/'encoded_media.host.o'")
template=template.replace("media/'memory_video.c'","media/'encoded_media.c'")
template=template.replace("run('native-fixture',command)","run('native-fixture',command)\nrun('native-demux',['clang-22',*native['compiler_flags'],*includes,'-c',media/'encoded_media.c','-o',OUT/'encoded-media.o'])")
template=template.replace("link.insert(link.index('--start-group'),str(OUT/'fragmented-mp4.o'))", "link.insert(link.index('--start-group'),str(OUT/'fragmented-mp4.o'))\nlink.insert(link.index('--start-group'),str(OUT/'encoded-media.o'))")
template=template.replace("BASE=ROOT/'build/dihscover-streaming-validation'","BASE=ROOT/'build/"+output+"'")
template=template.replace('SACX fMP4: split appends, fragment decoding, timestamps, backpressure, abort and malformed input OK',
    'SACX encoded samples: init metadata, AVC config, owned packets and rational PTS DTS across fragments OK')
template=template.replace("report={'linked':False", "from make_encoded_fixture import extras\nwith (OUT/'fragment_fixture.inc').open('a') as include:\n    for name,data in extras():\n        include.write('static const unsigned char '+name+'[]={'+','.join(str(b) for b in data)+'};\\n')\nreport={'linked':False",1)
template=template.replace("script=OUT/'run-native.py';script.write_text(text)","script=OUT/'run-native.py';script.write_text(text)\n    (OUT/'fixture-build-report.json').write_bytes((OUT/'fixture-report.json').read_bytes())")
if decoder:
    template=template.replace("['fragmented_mp4','encoded_media']","['fragmented_mp4','encoded_media','sample_decoder']")
    template=template.replace("OUT/'encoded_media.host.o'","OUT/'encoded_media.host.o',OUT/'sample_decoder.host.o'")
    template=template.replace("run('native-demux',", "run('native-sample-decoder',['clang-22',*native['compiler_flags'],*includes,'-c',media/'sample_decoder.c','-o',OUT/'sample-decoder.o'])\nrun('native-demux',")
    template=template.replace("link.insert(link.index('--start-group'),str(OUT/'encoded-media.o'))", "link.insert(link.index('--start-group'),str(OUT/'encoded-media.o'))\nlink.insert(link.index('--start-group'),str(OUT/'sample-decoder.o'))")
    template=template.replace('SACX encoded samples: init metadata, AVC config, owned packets and rational PTS DTS across fragments OK',
        'SACX persistent decoder: encoded tracks, fragment continuity, BGRA timing, backpressure and flush OK')
runner=OUT/'run-fixture.py';runner.write_text(template)
result=subprocess.call([sys.executable,str(runner),*[a for a in sys.argv[1:] if a!='--decoder']],cwd=ROOT)
if result:raise SystemExit(result)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report=json.loads((OUT/'fixture-report.json').read_text())
report['fixture_runner_sha256']=sha(runner)
report['inputs'].update({p.relative_to(ROOT).as_posix():sha(p) for p in [Path(__file__),MEDIA/'test_fragments.py',MEDIA/'make_encoded_fixture.py',MEDIA/'encoded_media.h',MEDIA/'encoded_media.c',MEDIA/'encoded_fixture.cpp']})
if decoder:
    report['persistent_decoder_tested']=True
    report['inputs'].update({p.relative_to(ROOT).as_posix():sha(p) for p in [MEDIA/'sample_decoder.h',MEDIA/'sample_decoder.c',MEDIA/'sample_decoder_fixture.cpp']})
(OUT/'fixture-report.json').write_text(json.dumps(report,indent=2)+'\n')
