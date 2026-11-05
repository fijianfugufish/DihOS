"""Build and validate the bounded-damage browser candidate in Ubuntu WSL."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--reuse-controls',action='store_true');p.add_argument('--built-renderer',action='store_true');p.add_argument('--software-view-transitions',action='store_true');p.add_argument('--output',type=Path,default=Path('build/dihscover-damage-validation'));args=p.parse_args()
OUT=(ROOT/args.output).resolve();assert OUT.is_relative_to(ROOT/'build')
OUT.mkdir(exist_ok=True)
host=OUT/'window-host';host.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
steps=[]
for name,sources in [('windows',['tools/user_window_tests.c','kernel/process/dihos_user_window.c']),
                     ('syscalls',['tools/user_sacx_tests.c','kernel/process/dihos_user_sacx.c'])]:
    command=['gcc','-std=c11','-g','-fsanitize=address,undefined','-Iinclude','-Ikernel/include','-Isdk/sacx/include',*sources,'-o',str(host/name)]
    with (host/(name+'.log')).open('w') as log:
        subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        subprocess.run([host/name],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    steps.append(command)
names=['tools/user_window_tests.c','kernel/process/dihos_user_window.c','tools/user_sacx_tests.c','kernel/process/dihos_user_sacx.c',
       'sdk/sacx/include/sacx_user_window.h','tools/webcore_dihos/test_damage.py','tools/browser_repaint_tests.cpp','tools/webcore_dihos/browser_repaint.h']
with (host/'repaint.log').open('w') as log:
    subprocess.run(['g++','-std=c++20','-g','-fsanitize=address,undefined','tools/browser_repaint_tests.cpp','-o',str(host/'repaint')],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    subprocess.run([host/'repaint'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
(host/'report.json').write_text(json.dumps({'passed':True,'sanitizers':['address','undefined'],'inputs':{n:sha(ROOT/n) for n in names},'commands':steps},indent=2)+'\n')
if args.built_renderer:
    report=json.loads((OUT/'renderer/report.json').read_text());assert report['linked']
    assert report['builder_sha256']==sha(ROOT/'tools/webcore_dihos/media/build_renderer.py')
    for collection in ['sources','linked_inputs','dependency_stamps']:
        for n,h in report[collection].items():assert sha(Path(n))==h,n
    assert sha(OUT/'renderer/page.elf')==report['elf_sha256']
    assert sha(OUT/'renderer/video-page.elf')==report['video_page_sha256']
    if args.software_view_transitions:assert report['software_view_transitions']
steps=[('media/build_renderer.py',['--mse','--resume']+(['--software-view-transitions'] if args.software_view_transitions else [])),('test_optional_abort.py',[]),
                     ('test_optional_abort.py',['--indexeddb-control']),('media/test_mse_page.py',[]),
                     ('media/test_mse_regressions.py',[]),('media/prepare_mse_frontend.py',[])]
if args.software_view_transitions:steps.insert(1,('test_view_transitions.py',[]))
for script,flags in steps:
    if args.built_renderer and script=='media/build_renderer.py':continue
    if args.reuse_controls and script=='test_optional_abort.py':
        report=json.loads((OUT/('indexeddb-control' if flags else 'abort-test')/'report.json').read_text())
        assert report['passed'] and all(sha(ROOT/n)==h for n,h in report['inputs'].items())
        continue
    subprocess.run(['python3',str(ROOT/'tools/webcore_dihos'/script),'--output',str(OUT),*flags],cwd=ROOT,check=True)
assert 'PASS: consent-shaped fetch chain:' in (OUT/'qemu-page/run.log').read_text()
print('PASS: bounded damage ABI, native WebCore consent-shaped fetch, browser and media regressions')
