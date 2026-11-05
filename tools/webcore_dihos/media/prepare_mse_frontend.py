"""Expose the executed streaming renderer to the existing Dihscover toolbar."""
from pathlib import Path
import argparse,hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path);args=p.parse_args()
OUT=(ROOT/args.output).resolve() if args.output else ROOT/'build/dihscover-mse-player'
assert OUT.is_relative_to(ROOT/'build');renderer=OUT/'renderer'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
report=read(renderer/'report.json');video=read(OUT/'qemu/report.json')
regression=read(OUT/'qemu-page/report.json')
engineReport=read(ROOT/'build/dihscover-mse-player-engine/report.json')
assert engineReport['engine_built'] and engineReport['media_source_enabled']
assert report['engine_report_sha256']==sha(ROOT/'build/dihscover-mse-player-engine/report.json')
assert report['linked'] and report['elf_sha256']==sha(renderer/'page.elf')
assert video['passed'] and video['javascript_mse_executed']
assert video['elf_sha256']==report['video_page_sha256']
assert video['renderer_report_sha256']==sha(renderer/'report.json')
assert video['log_sha256']==sha(OUT/'qemu/run.log')
assert regression['passed'] and regression['image_test_sha256']==report['elf_sha256']
assert regression['fixture_sha256']==report['fixture_elf_sha256']
for item in [video,regression,read(OUT/'elf-tests/report.json')]:
    assert item['passed']
    for name,digest in item['inputs'].items():assert sha(ROOT/name)==digest,name
for name in ['loader-check.log','loader-check-video-page.log','loader-check-resource.log']:
    assert read(renderer/name)['result']==0
engine=OUT/'engine';engine.mkdir(exist_ok=True)
shutil.copy2(renderer/'page.elf',engine/'page.elf')
(engine/'report.json').write_text(json.dumps({'embedded':True,'linked':True,'loader_accepted':True,
    'elf_sha256':report['elf_sha256'],'elf_bytes':(engine/'page.elf').stat().st_size,
    'native_link_report_sha256':sha(renderer/'report.json'),
    'mse_execution_sha256':sha(OUT/'qemu/report.json'),
    'browser_regression_sha256':sha(OUT/'qemu-page/report.json'),
    'muted_mp4_h264_tested':True,'media_source_implemented':True,
    'youtube_playback_tested':False},indent=2)+'\n')
print('Prepared tested streaming renderer for the existing Dihscover frontend')
