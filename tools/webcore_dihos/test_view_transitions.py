"""Real ARM64 Page control/fallback tests for compositor-independent navigation."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);args=p.parse_args()
BASE=(ROOT/args.output).resolve();assert BASE.is_relative_to(ROOT/'build')
renderer=json.loads((BASE/'renderer/report.json').read_text())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert renderer['software_view_transitions']
for control in [True,False]:
    label='view-transition-control' if control else 'view-transition'
    OUT=BASE/(label+'-test');(OUT/'native/renderer').mkdir(parents=True,exist_ok=True)
    native=OUT/'native';elf=BASE/'renderer'/(label+'.elf')
    assert sha(elf)==renderer['transition_fixtures'][label]
    shutil.copy2(elf,native/'renderer/video-page.elf')
    (native/'renderer/report.json').write_text(json.dumps({**renderer,'video_page_sha256':sha(elf)}))
    subprocess.run(['python3','tools/webcore_dihos/prepare_large_test_inputs.py','--output',str(native)],cwd=ROOT,check=True)
    harness=(ROOT/'tools/user_runtime_qemu_tests.c').read_text()
    assert harness.count('infos[0].exit_status!=124')==1 and harness.count('infos[0].exit_status==124')==1
    if control:
        harness=harness.replace('infos[0].exit_status!=124','infos[0].exit_status!=180').replace('infos[0].exit_status==124','infos[0].exit_status==180')
    marker='        print("PASS: native WebCore HTML video, MP4/H.264 pixels, seek, timed muted playback, ended events and process cleanup\\n");'
    assert harness.count(marker)==1
    expected='Page: unexpected accelerated graphics layer' if control else 'SACX software view transitions:'
    success='PASS: unpatched view transition reproduces graphics-layer exit 180 and cleans up' if control else 'PASS: software view transitions preserve DOM callbacks, promises, painting and cleanup'
    harness=harness.replace(marker,'        require(text_contains(infos[0].last_log,"'+expected+'"),"view transition fixture actually executed");\n        print("'+success+'\\n");')
    path=OUT/'harness.c';path.write_text(harness)
    template=(ROOT/'tools/webcore_dihos/media/test_page.py').read_text()
    template=template.replace('ROOT=Path(__file__).resolve().parents[3]','ROOT=Path('+repr(str(ROOT))+')',1)
    template=template.replace("BASE=ROOT/'build/dihscover-video-player'",'BASE=Path('+repr(str(native))+')')
    template=template.replace("'tools/user_runtime_qemu_tests.c'",repr(path.relative_to(ROOT).as_posix()))
    template=template.replace('PASS: native WebCore HTML video','PASS: '+('unpatched view transition' if control else 'software view transitions'))
    runner=OUT/'run.py';runner.write_text(template)
    with (OUT/'native.log').open('w') as log:subprocess.run(['python3',str(runner)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    report=json.loads((native/'qemu/report.json').read_text());assert report['passed']
    text=(native/'qemu/run.log').read_text();assert success in text
    names=['tools/webcore_dihos/test_view_transitions.py','tools/webcore_dihos/view_transition_fixture.h',
           'tools/webcore_dihos/headless_page_fixture.cpp','tools/webcore_dihos/software_view_transitions.py']
    (OUT/'report.json').write_text(json.dumps({'passed':True,'real_arm64_el0':True,'unpatched_control':control,
        'expected_exit':180 if control else 124,'elf_sha256':sha(elf),'inputs':{n:sha(ROOT/n) for n in names},
        'native_report_sha256':sha(native/'qemu/report.json'),'log_sha256':sha(native/'qemu/run.log')},indent=2)+'\n')
    print(success,flush=True)
