"""Compile actual configured WebCore targets and retain bounded failure logs."""
from pathlib import Path
import argparse,hashlib,json,os,platform,signal,subprocess,time
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--timeout',type=int,default=300)
parser.add_argument('--jobs',type=int,choices=range(1,9),default=4)
parser.add_argument('--target',action='append')
parser.add_argument('--report',type=Path,default=ROOT/'build/webcore-native/report.json')
args=parser.parse_args()
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report_path=args.report;configured=json.loads(report_path.read_text())
if not configured.get('webcore_configured') or configured['target']!='aarch64-none-elf':
    raise SystemExit('Successful native WebCore configuration required')
if configured['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/configure_native.py'):
    raise SystemExit('Configure builder changed; reconfigure')
for field,name in [('webcore_edits_sha256','port-edits.json'),('image_edits_sha256','image-edits.json'),('engine_edits_sha256','engine-edits.json')]:
    if configured[field]!=sha(ROOT/'tools/webcore_dihos'/name):raise SystemExit('Port edits changed; reconfigure')
build=Path(configured['build']);targets=(report_path.parent/'targets.txt').read_text().splitlines()
selected=args.target or [next(line.split(':')[0] for line in targets if line.endswith(': CXX_COMPILER__WebCoreDOMAndRendering_unscanned_Release') and '/HTMLTokenizer.cpp.obj' in line),
                         next(line.split(':')[0] for line in targets if '/CSSTokenizer.cpp.obj:' in line)]
if 'WebCore' in selected and 'JavaScriptCoreJIT' not in selected:
    # Upstream keeps interpreter/bytecode objects in this target even with JIT
    # disabled. Static archives do not absorb transitive object libraries.
    selected.append('JavaScriptCoreJIT')
out=report_path.parent;log=out/'compile.log'
command=['ninja','-C',str(build),'-j',str(args.jobs),*selected]
started=time.monotonic()
with log.open('w') as stream:
    process=subprocess.Popen(command,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
    try:code=process.wait(args.timeout)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid,signal.SIGTERM)
        try:process.wait(5)
        except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
        code=124
result={'target':'aarch64-none-elf','configure_report_sha256':sha(report_path),
        'builder_sha256':sha(Path(__file__)),'argv':command,'exit_code':code,
        'seconds':round(time.monotonic()-started,2),'log_sha256':sha(log),
        'objects':{t:sha(build/t) for t in selected if (build/t).is_file()},
        'dom_executed':False,'page_rendered':False,'shipping_browser_changed':False}
(out/'compile-report.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result),flush=True)
raise SystemExit(code)
