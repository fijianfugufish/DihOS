"""Exercise the exact freestanding TLS implementation using host sockets.

Only the socket/clock/entropy/physical-page shims are host implementations.
The guest's TCP/NetVSC delivery is tested separately and still needs a VM run.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=ROOT/'build/kernel-tls-host');p.add_argument('--path',default='/');p.add_argument('hosts',nargs='*',default=['www.google.com','example.com','www.netsurf-browser.org','html.duckduckgo.com']);a=p.parse_args()
assert a.path.startswith('/') and len(a.path)<=8191 and '\r' not in a.path and '\n' not in a.path
out=a.output.resolve()
report=json.loads((out/'report.json').read_text());assert report['compiled'] and report['host']
if report.get('extra_ca'):assert hashlib.sha256(Path(report['extra_ca']['header']).read_bytes()).hexdigest()==report['extra_ca']['sha256']
for name,digest in report['inputs'].items():assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest,name
for name,digest in report['objects'].items():assert hashlib.sha256(Path(name).read_bytes()).hexdigest()==digest,name
args=[report['compiler'],'-O2','-I'+str(ROOT/'kernel/include'),str(ROOT/'tools/kernel_tls_socket_probe.c'),str(ROOT/'kernel/net/http_stream.c'),*report['objects'],'-o',str(out/'socket-probe')]
with (out/'link-probe.log').open('w') as log:subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,check=True)
results=[]
for host in a.hosts:
    # Keep filenames inside the output directory even with arbitrary CLI input.
    assert host and all(c.isalnum() or c in '.-' for c in host)
    result=subprocess.run([str(out/'socket-probe'),host,'443',a.path],capture_output=True,text=True,timeout=50)
    (out/(host+'.log')).write_text(result.stdout+result.stderr)
    results.append({'host':host,'path':a.path,'exit_code':result.returncode,'output':result.stdout+result.stderr})
    print(result.stdout+result.stderr,flush=True)
(out/'live-results.json').write_text(json.dumps(results,indent=2)+'\n')
raise SystemExit(any(r['exit_code'] for r in results))
