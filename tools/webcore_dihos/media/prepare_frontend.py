"""Expose a tested renderer to the existing Dihscover frontend build tool."""
from pathlib import Path
import hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/dihscover-video-player';renderer=OUT/'renderer'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report=json.loads((renderer/'report.json').read_text())
video=json.loads((OUT/'qemu/report.json').read_text())
regression=json.loads((OUT/'qemu-page/report.json').read_text())
assert report['linked'] and report['elf_sha256']==sha(renderer/'page.elf')
assert video['passed'] and video['elf_sha256']==report['video_page_sha256']
assert video['renderer_report_sha256']==sha(renderer/'report.json')
assert regression['passed'] and regression['image_test_sha256']==report['elf_sha256']
assert regression['fixture_sha256']==report['fixture_elf_sha256']
for name in ['loader-check.log','loader-check-video-page.log','loader-check-resource.log']:
    result=json.loads((renderer/name).read_text());assert result['result']==0
assert video['log_sha256']==sha(OUT/'qemu/run.log')
for item in [video,regression]:
    for name,digest in item['inputs'].items():assert sha(ROOT/name)==digest,name
engine=OUT/'engine';engine.mkdir(exist_ok=True)
target=engine/'page.elf'
if not target.exists():shutil.copy2(renderer/'page.elf',target)
assert sha(target)==report['elf_sha256']
(engine/'report.json').write_text(json.dumps({'embedded':True,'linked':True,'loader_accepted':True,
    'elf_sha256':report['elf_sha256'],'elf_bytes':target.stat().st_size,
    'native_link_report_sha256':sha(renderer/'report.json'),
    'video_execution_sha256':sha(OUT/'qemu/report.json'),
    'browser_regression_sha256':sha(OUT/'qemu-page/report.json'),
    'muted_mp4_h264_tested':True,'media_source_implemented':False,
    'youtube_playback_tested':False},indent=2)+'\n')
print('Prepared tested, loader-accepted video renderer for the existing browser frontend')
