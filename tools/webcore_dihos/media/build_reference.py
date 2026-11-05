"""Build a host-only FFmpeg reference for checking owned test clips.

No output from this build is linked or packaged into DihOS. The production
decoder is built separately for aarch64-none-elf by build_media_decoder.py.
"""
from pathlib import Path
import hashlib,json,subprocess
from make_fixture import clip
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/dihscover-media-validation'
native=json.loads((OUT/'decoder-report.json').read_text())
source=Path(native['source']);build=source.parent/'host-reference';build.mkdir(exist_ok=True)
def run(label,args):
    with (OUT/(label+'.log')).open('w') as log:
        code=subprocess.call(list(map(str,args)),cwd=build,stdout=log,stderr=subprocess.STDOUT)
    if code:raise SystemExit('Inspect '+str(OUT/(label+'.log')))
    print(label,'OK',flush=True)
run('reference-configure',[source/'configure','--cc=clang-22','--disable-autodetect',
    '--disable-everything','--disable-doc','--disable-network','--disable-asm',
    '--disable-inline-asm','--disable-shared','--enable-static','--enable-ffmpeg',
    '--enable-decoder=h264','--enable-demuxer=mov','--enable-parser=h264',
    '--enable-encoder=rawvideo','--enable-muxer=rawvideo','--enable-protocol=file',
    '--enable-filter=null,format,scale','--disable-avdevice'])
run('reference-build',['make','-j','6','ffmpeg'])
run('reference-decode',[build/'ffmpeg','-v','error','-y','-i',OUT/'red-blue.mp4',
                       '-an','-pix_fmt','bgra','-f','rawvideo',OUT/'reference.bgra'])
pixels=(OUT/'reference.bgra').read_bytes();frame_size=64*48*4
assert len(pixels)==10*frame_size,len(pixels)
for index in range(10):
    b,g,r,a=pixels[index*frame_size:index*frame_size+4]
    assert (r>245 and b<10 and g<10 and a==255) if index<5 else (b>245 and r<10 and g<10 and a==255),(index,b,g,r,a)
(OUT/'reference-report.json').write_text(json.dumps({'host_reference_only':True,
    'fixture_sha256':hashlib.sha256((OUT/'red-blue.mp4').read_bytes()).hexdigest(),
    'frames':10,'frame_bytes':frame_size,'bgra_sha256':hashlib.sha256(pixels).hexdigest()},indent=2)+'\n')
print('PASS: owned MP4 reference decodes ten red/blue H.264 frames',flush=True)
hd=clip(1920,1080,2);(OUT/'red-blue-1080.mp4').write_bytes(hd)
run('reference-decode-1080',[build/'ffmpeg','-v','error','-y','-i',OUT/'red-blue-1080.mp4',
                       '-an','-pix_fmt','bgra','-f','rawvideo',OUT/'reference-1080.bgra'])
pixels=(OUT/'reference-1080.bgra').read_bytes();size=1920*1080*4
assert len(pixels)==size*2
for index in range(2):
    b,g,r,a=pixels[index*size:index*size+4]
    assert (r>245 and b<10 and g<10 and a==255) if not index else (b>245 and r<10 and g<10 and a==255)
report=json.loads((OUT/'reference-report.json').read_text())
report['hd_fixture_sha256']=hashlib.sha256(hd).hexdigest();report['hd_frames']=2
(OUT/'reference-report.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: owned 1920x1080 H.264 SPS crop and red/blue frames',flush=True)
