"""Capture public page styles for loader/parser diagnostics, never media."""
from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import urljoin
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);args=p.parse_args()
OUT=(ROOT/args.output/'captured-css').resolve();assert OUT.is_relative_to(ROOT/'build');OUT.mkdir(parents=True,exist_ok=True)
def fetch(url,name):
    path=OUT/name
    r=subprocess.run(['curl','--location','--compressed','--max-time','45','--silent','--show-error',
                      '--dump-header',str(path.with_suffix('.headers')),'--output',str(path),
                      '--write-out','%{http_code}',url],cwd=ROOT,text=True,capture_output=True)
    return {'url':url,'path':name,'status':r.stdout,'exit_code':r.returncode,'error':r.stderr,
            'size':path.stat().st_size if path.exists() else 0,
            'sha256':hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None}
page=fetch('https://www.youtube.com/','youtube.html')
class Styles(HTMLParser):
    def __init__(self):super().__init__();self.urls=[]
    def handle_starttag(self,tag,attrs):
        a=dict(attrs)
        if tag=='link' and 'stylesheet' in a.get('rel','').split() and a.get('href'):
            self.urls.append(urljoin('https://www.youtube.com/',a['href']))
parser=Styles();parser.feed((OUT/'youtube.html').read_text(errors='replace'))
styles=[]
for index,url in enumerate(dict.fromkeys(parser.urls)):
    result=fetch(url,'style-'+str(index)+'.css');styles.append(result)
    print(result['status'],result['size'],url,flush=True)
(OUT/'report.json').write_text(json.dumps({'page':page,'styles':styles},indent=2)+'\n')
