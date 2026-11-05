"""Owned fragmented MP4 fixture; no fetched video or host encoder dependency."""
from make_fixture import clip,box,full,u32
import struct

def boxes(data):
    pos=0
    while pos<len(data):
        size=struct.unpack_from('>I',data,pos)[0]
        assert size>=8 and pos+size<=len(data)
        yield data[pos+4:pos+8],data[pos+8:pos+size]
        pos+=size
    assert pos==len(data)

def fragmented(frames=10,duration_ms=200):
    assert frames>=2 and frames%2==0 and duration_ms>0
    source=clip(frames=frames);top=dict(boxes(source));moov=dict(boxes(top[b'moov']))
    trak=dict(boxes(moov[b'trak']));mdia=dict(boxes(trak[b'mdia']))
    minf=dict(boxes(mdia[b'minf']));stbl=dict(boxes(minf[b'stbl']))
    stsz=stbl[b'stsz'];count=struct.unpack_from('>I',stsz,8)[0]
    sizes=struct.unpack_from('>'+str(count)+'I',stsz,12)
    samples=[];pos=0
    for size in sizes:samples.append(top[b'mdat'][pos:pos+size]);pos+=size
    assert pos==len(top[b'mdat']) and len(samples)==frames
    def movie_duration(data,offset):return data[:offset]+u32(frames*duration_ms)+data[offset+4:]
    tables=box('stsd',stbl[b'stsd'])+full('stts',u32(0))+full('stsc',u32(0))+full('stsz',u32(0)*2)+full('stco',u32(0))
    contents=box('minf',box('vmhd',minf[b'vmhd'])+box('dinf',minf[b'dinf'])+box('stbl',tables))
    init=box('ftyp',b'iso6'+u32(1)+b'iso6mp41dash')+box('moov',
        box('mvhd',movie_duration(moov[b'mvhd'],16))+box('trak',box('tkhd',movie_duration(trak[b'tkhd'],20))+
            box('mdia',box('mdhd',movie_duration(mdia[b'mdhd'],16))+box('hdlr',mdia[b'hdlr'])+contents))+
        box('mvex',full('trex',u32(1)+u32(1)+u32(duration_ms)+u32(0)+u32(0x02000000))))
    segments=[]
    for sequence,start in enumerate([0,frames//2],1):
        selected=samples[start:start+frames//2]
        def fragment(offset):
            trun=full('trun',u32(len(selected))+u32(offset)+b''.join(u32(duration_ms)+u32(len(s))+u32(0x02000000) for s in selected),0x701)
            return box('moof',full('mfhd',u32(sequence))+box('traf',full('tfhd',u32(1),0x020000)+
                full('tfdt',struct.pack('>Q',start*duration_ms),0x01000000)+trun))
        moof=fragment(0);moof=fragment(len(moof)+8)
        segments.append(moof+box('mdat',b''.join(selected)))
    return init,segments

if __name__=='__main__':
    from pathlib import Path
    import argparse
    p=argparse.ArgumentParser();p.add_argument('output',type=Path);args=p.parse_args()
    init,segments=fragmented();args.output.write_bytes(init+b''.join(segments))
    print('Generated owned fragmented MP4: init + two moof/mdat segments, absolute tfdt timestamps')
