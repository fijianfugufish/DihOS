"""Generate an owned H.264 baseline/MP4 red-to-blue clip without external media.

I_PCM intra macroblocks deliberately avoid an encoder dependency. The decoder
must still parse real MP4 sample tables, AVCC, SPS/PPS and H.264 slice syntax.
"""
from pathlib import Path
import struct

def u32(n): return struct.pack('>I', n)
def box(kind, payload): return u32(8+len(payload))+kind.encode('ascii')+payload
def full(kind, payload, flags=0): return box(kind,u32(flags)+payload)
class Bits:
    def __init__(self): self.bits=[];self.chunks=[]
    def put(self,n,count): self.bits.extend((n>>i)&1 for i in range(count-1,-1,-1))
    def ue(self,n):
        value=n+1; length=value.bit_length();self.put(0,length-1);self.put(value,length)
    def se(self,n): self.ue(-2*n if n<=0 else 2*n-1)
    def align(self):
        while len(self.bits)%8:self.put(0,1)
    def data(self):
        return b''.join(self.chunks)+bytes(sum(self.bits[i+j]<<(7-j) for j in range(8)) for i in range(0,len(self.bits),8))
    def raw(self,data):
        assert len(self.bits)%8==0
        self.chunks.append(bytes(sum(self.bits[i+j]<<(7-j) for j in range(8)) for i in range(0,len(self.bits),8)))
        self.bits=[];self.chunks.append(data)
    def finish(self): self.put(1,1);self.align();return self.data()
def escape(data):
    out=bytearray();zeros=0
    for byte in data:
        if zeros>=2 and byte<=3:out.append(3);zeros=0
        out.append(byte);zeros=zeros+1 if byte==0 else 0
    return bytes(out)
def clip(width=64,height=48,frames=10):
    coded_height=(height+15)&~15
    assert width%16==0 and height%2==0
    level=40 if width>352 else 10
    b=Bits();b.put(66,8);b.put(0xc0,8);b.put(level,8);b.ue(0);b.ue(0);b.ue(2);b.ue(0)
    b.put(0,1);b.ue(width//16-1);b.ue(coded_height//16-1);b.put(1,1);b.put(1,1)
    b.put(int(height!=coded_height),1)
    if height!=coded_height:b.ue(0);b.ue(0);b.ue(0);b.ue((coded_height-height)//2)
    b.put(0,1)
    sps=b'\x67'+escape(b.finish())
    b=Bits();b.ue(0);b.ue(0);b.put(0,1);b.put(0,1);b.ue(0);b.ue(0);b.ue(0)
    b.put(0,1);b.put(0,2);b.se(0);b.se(0);b.se(0);b.put(1,1);b.put(0,1);b.put(0,1)
    pps=b'\x68'+escape(b.finish())
    samples=[]
    for index in range(frames):
        b=Bits();b.ue(0);b.ue(2);b.ue(0);b.put(0,4);b.ue(0);b.put(0,1);b.put(0,1);b.se(0);b.ue(1)
        y,u,v=(81,90,240) if index<frames//2 else (41,240,110)
        for _ in range((width//16)*(coded_height//16)):
            b.ue(25);b.align()
            b.raw(bytes([y])*256+bytes([u])*64+bytes([v])*64)
        nal=b'\x65'+escape(b.finish());samples.append(u32(len(nal))+nal)
    ftyp=box('ftyp',b'isom'+u32(512)+b'isomiso2avc1mp41')
    mdat=box('mdat',b''.join(samples));offset=len(ftyp)+8
    matrix=b''.join(u32(v) for v in [65536,0,0,0,65536,0,0,0,0x40000000])
    mvhd=full('mvhd',u32(0)*2+u32(1000)+u32(frames*200)+u32(65536)+struct.pack('>HH',256,0)+b'\0'*8+matrix+b'\0'*24+u32(2))
    tkhd=full('tkhd',u32(0)*2+u32(1)+u32(0)+u32(frames*200)+b'\0'*8+b'\0'*8+matrix+u32(width<<16)+u32(height<<16),7)
    mdhd=full('mdhd',u32(0)*2+u32(1000)+u32(frames*200)+struct.pack('>HH',0x55c4,0))
    hdlr=full('hdlr',u32(0)+b'vide'+b'\0'*12+b'DihOS owned fixture\0')
    avcc=box('avcC',bytes([1,66,0xc0,level,0xff,0xe1])+struct.pack('>H',len(sps))+sps+b'\x01'+struct.pack('>H',len(pps))+pps)
    avc1=box('avc1',b'\0'*6+struct.pack('>H',1)+b'\0'*16+struct.pack('>HH',width,height)+u32(72<<16)*2+u32(0)+struct.pack('>H',1)+b'\0'*32+struct.pack('>HH',24,65535)+avcc)
    stbl=box('stbl',full('stsd',u32(1)+avc1)+full('stts',u32(1)+u32(frames)+u32(200))+
             full('stsc',u32(1)+u32(1)+u32(frames)+u32(1))+full('stsz',u32(0)+u32(frames)+b''.join(u32(len(s)) for s in samples))+
             full('stco',u32(1)+u32(offset))+full('stss',u32(frames)+b''.join(u32(i+1) for i in range(frames))))
    dinf=box('dinf',full('dref',u32(1)+full('url ',b'',1)))
    minf=box('minf',full('vmhd',b'\0'*8,1)+dinf+stbl)
    return ftyp+mdat+box('moov',mvhd+box('trak',tkhd+box('mdia',mdhd+hdlr+minf)))
if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('output',type=Path);args=p.parse_args()
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(clip())
    print('Generated 64x48 H.264 baseline clip: 10 frames, 2 seconds, red then blue')
