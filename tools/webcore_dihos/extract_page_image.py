"""Decode the native diagnostic's checked-log pixel export into a PNG.

No page rendering happens here: rows contain the guest's Cairo surface bytes.
Missing, duplicate or reordered chunks and unsuccessful runs are rejected.
"""
from pathlib import Path
import argparse,binascii,hashlib,json,re,struct,zlib
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--log',type=Path,default=ROOT/'build/user-runtime/qemu.log');parser.add_argument('--output',type=Path,default=ROOT/'build/webcore-headless-page/page.png');args=parser.parse_args()
data=args.log.read_bytes()
text=data.decode('utf-16' if data.startswith((b'\xff\xfe',b'\xfe\xff')) else 'utf-8-sig')
if 'FAIL:' in text or 'PASS: native WebCore Page, HTML parsing, JavaScript DOM/style mutation, layout, memory fonts and guarded offscreen painting' not in text:
    raise SystemExit('A passing native Page runtime test is required')
report=json.loads((ROOT/'build/webcore-headless-page/report.json').read_text())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if not report.get('capture') or not report.get('loader_accepted') or report['elf_sha256']!=sha(ROOT/'build/webcore-headless-page/page.elf'):
    raise SystemExit('Current loader-validated capture executable required')
pixels=bytearray();width=height=0;active=complete=False
for line in text.splitlines():
    if line.startswith('Page: '):line=line[6:]
    if line.startswith('PAGE_RGB_BEGIN '):
        if active or complete:raise SystemExit('Duplicate frame')
        match=re.fullmatch(r'PAGE_RGB_BEGIN (\d+) (\d+)',line)
        if not match:raise SystemExit('Invalid frame header')
        width,height=map(int,match.groups())
        if not (0<width<=2048 and 0<height<=2048):raise SystemExit('Frame dimensions exceed diagnostic bound')
        active=True
    elif line.startswith('PIX:'):
        match=re.fullmatch(r'PIX:([0-9a-f]{8}):([0-9a-f]{6,240})',line)
        if not active or not match or len(match[2])%6 or int(match[1],16)!=len(pixels)//3:raise SystemExit('Missing, malformed or reordered pixel chunk')
        pixels.extend(bytes.fromhex(match[2]))
        if len(pixels)>width*height*3:raise SystemExit('Pixel export exceeds frame')
    elif line=='PAGE_RGB_END':
        if not active or len(pixels)!=width*height*3:raise SystemExit('Incomplete frame')
        active=False;complete=True
if not complete:raise SystemExit('No complete native frame captured')
def chunk(kind,payload):return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',binascii.crc32(kind+payload)&0xffffffff)
rows=b''.join(b'\0'+pixels[row*width*3:(row+1)*width*3] for row in range(height))
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'')
args.output.parent.mkdir(exist_ok=True);args.output.write_bytes(png)
metadata={'source':'Native DihOS WebCore Cairo surface exported over checked log calls','width':width,'height':height,
          'rgb_sha256':hashlib.sha256(pixels).hexdigest(),'log_sha256':hashlib.sha256(data).hexdigest(),
          'elf_sha256':report['elf_sha256'],'png_sha256':sha(args.output),'host_rendering':False}
args.output.with_suffix('.capture.json').write_text(json.dumps(metadata,indent=2)+'\n')
print('Native Page surface extracted:',str(args.output),width,height)
