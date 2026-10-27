"""Separate host-only Servo probe. Never a DihOS rendering helper.

Run with --servo PATH. Stores JSON, screenshot and complete engine log.
The local deterministic server can also be used for DihOS HTTP smoke tests.
"""
import argparse, contextlib, hashlib, http.server, json, pathlib, subprocess, threading, time, urllib.request
ROOT=pathlib.Path(__file__).resolve().parents[1]
class FixtureServer(http.server.SimpleHTTPRequestHandler):
    def __init__(self,*args,**kw):super().__init__(*args,directory=str(ROOT/'tools/browser_fixtures'),**kw)
    def translate_path(self,path):
        if path.startswith('/fonts/'):return str(ROOT/'OS/System/Fonts/Web'/path.split('/')[-1])
        return super().translate_path(path)
    def do_GET(self):
        if self.path.startswith('/redirect'):
            self.send_response(302);self.send_header('Location','/layout.html');self.send_header('Content-Length','0');self.end_headers()
        elif self.path.startswith('/stall'):
            self.send_response(200);self.send_header('Content-Length','100');self.end_headers();time.sleep(15)
        elif self.path.startswith('/huge'):
            data=('<!doctype html><meta charset=utf-8><title>Large document</title>'+''.join(f'<p>Paragraph {i}: layout and cancellation test.</p>' for i in range(20000))).encode()
            self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.end_headers()
            try:self.wfile.write(data)
            except (BrokenPipeError,ConnectionResetError):pass
        elif self.path.startswith('/slow'):
            self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.end_headers()
            for i in range(40):
                try:self.wfile.write(f'<p>Delayed fragment {i}</p>\n'.encode());self.wfile.flush();time.sleep(.1)
                except (BrokenPipeError,ConnectionResetError):break
        else:super().do_GET()
    def log_message(self,*args):pass
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--servo');ap.add_argument('--serve',action='store_true');ap.add_argument('--live',action='store_true');ap.add_argument('--port',type=int,default=8765);ap.add_argument('--bind',default='127.0.0.1');a=ap.parse_args()
    server=http.server.ThreadingHTTPServer((a.bind,a.port),FixtureServer)
    if a.serve:print(f'Fixtures: http://{a.bind}:{a.port}/layout.html',flush=True);server.serve_forever();return
    if not a.servo:ap.error('--servo is required for probing')
    threading.Thread(target=server.serve_forever,daemon=True).start()
    out=ROOT/'build/browser-engine-probe';out.mkdir(parents=True,exist_ok=True)
    executable=pathlib.Path(a.servo).resolve();result={'engine':'Servo 0.7.0','host_only':True,'exe_sha256':hashlib.sha256(executable.read_bytes()).hexdigest(),'tests':{}}
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}));session=None
    def request(path,data=None):
        req=urllib.request.Request('http://127.0.0.1:8766'+path,data=None if data is None else json.dumps(data).encode(),headers={'Content-Type':'application/json'})
        with opener.open(req,timeout=45) as response:return json.load(response).get('value')
    with open(out/'servo.log','w',encoding='utf-8') as log:
        process=subprocess.Popen([str(executable),'-z','--webdriver=8766','--window-size=1000x900','--config-dir='+str(out/'profile'),'--temporary-storage','about:blank'],stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        try:
            for _ in range(100):
                try:request('/status');break
                except Exception:
                    if process.poll() is not None:raise RuntimeError('Servo exited before WebDriver started')
                    time.sleep(.1)
            value=request('/session',{'capabilities':{'alwaysMatch':{}}});session=value['sessionId'];base='/session/'+session
            began=time.monotonic();request(base+'/url',{'url':f'http://127.0.0.1:{a.port}/layout.html'});
            result['local_navigation_ms']=round((time.monotonic()-began)*1000,2)
            result['tests']=request(base+'/execute/sync',{'script':'return window.fixtureResults()','args':[]})
            import base64
            (out/'layout.png').write_bytes(base64.b64decode(request(base+'/screenshot')))
            if a.live:
                result['sites']=[]
                request(base+'/timeouts',{'pageLoad':25000,'script':10000,'implicit':0})
                for name,url in [('wikipedia','https://en.wikipedia.org/wiki/Web_browser'),('github','https://github.com/servo/servo'),('mdn','https://developer.mozilla.org/en-US/docs/Web/CSS'),('duckduckgo','https://duckduckgo.com/?q=web+browser')]:
                    row={'name':name,'url':url};began=time.monotonic()
                    try:
                        request(base+'/url',{'url':url})
                        # Servo's navigation return is not a paint/load barrier.
                        # Keep the same fixed settle interval as the WebKit probe.
                        time.sleep(10)
                        row['dom']=request(base+'/execute/sync',{'script':'return {title:document.title,text:document.body.innerText.length,links:document.links.length,state:document.readyState}','args':[]})
                        (out/(name+'.png')).write_bytes(base64.b64decode(request(base+'/screenshot')))
                    except Exception as exc:row['error']=str(exc)
                    row['elapsed_ms']=round((time.monotonic()-began)*1000,2);result['sites'].append(row);print(json.dumps(row),flush=True)
            result['status']='ran'
        except Exception as exc:result['status']='blocked';result['error']=str(exc)
        finally:
            if session:
                with contextlib.suppress(Exception):opener.open(urllib.request.Request('http://127.0.0.1:8766/session/'+session,method='DELETE'),timeout=5).close()
            if process.poll() is None:process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
            server.shutdown()
    (out/'results.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
if __name__=='__main__':main()
