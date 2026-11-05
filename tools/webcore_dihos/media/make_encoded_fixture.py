"""Owned demux-only timing fixtures: signed CTS, long tfdt and AAC-LC metadata."""
from make_fragment_fixture import fragmented,boxes
from make_fixture import box,full,u32
import struct

LONG_BASE=(1<<32)+291
def long_timestamp():
    init,segments=fragmented();top=dict(boxes(segments[0]));moof=dict(boxes(top[b'moof']));traf=dict(boxes(moof[b'traf']))
    old=traf[b'trun'];count=struct.unpack_from('>I',old,4)[0]
    entries=b''.join(old[12+i*12:24+i*12]+struct.pack('>i',-50 if not i%2 else 50) for i in range(count))
    def build(offset):
        return box('moof',box('mfhd',moof[b'mfhd'])+box('traf',box('tfhd',traf[b'tfhd'])+
            full('tfdt',struct.pack('>Q',LONG_BASE),0x01000000)+full('trun',u32(count)+u32(offset)+entries,0x01000f01)))
    header=build(0);return init+build(len(header)+8)+box('mdat',top[b'mdat'])

def audio(count=4):
    assert 1<=count<=4096
    init,_=fragmented();top=dict(boxes(init));moov=dict(boxes(top[b'moov']));trak=dict(boxes(moov[b'trak']));mdia=dict(boxes(trak[b'mdia']))
    # The elementary payload is only demuxed; this fixture makes no claim of
    # validating AAC decoding or producing sound.
    def descriptor(tag,data):
        assert len(data)<128
        return bytes([tag,len(data)])+data
    asc=b'\x12\x10' # AAC-LC, 44100 Hz, stereo.
    decoder=descriptor(4,b'\x40\x15'+b'\0'*3+u32(64000)*2+descriptor(5,asc))
    esds=full('esds',descriptor(3,b'\0\x01\0'+decoder+descriptor(6,b'\x02')))
    mp4a=box('mp4a',b'\0'*6+struct.pack('>H',1)+b'\0'*8+struct.pack('>HHHH',2,16,0,0)+u32(44100<<16)+esds)
    stbl=box('stbl',full('stsd',u32(1)+mp4a)+full('stts',u32(0))+full('stsc',u32(0))+full('stsz',u32(0)*2)+full('stco',u32(0)))
    dinf=box('dinf',full('dref',u32(1)+full('url ',b'',1)))
    mdhd=full('mdhd',u32(0)*2+u32(44100)+u32(1024*count)+struct.pack('>HH',0x55c4,0))
    hdlr=full('hdlr',u32(0)+b'soun'+b'\0'*12+b'DihOS timing fixture\0')
    init=box('ftyp',top[b'ftyp'])+box('moov',box('mvhd',moov[b'mvhd'])+box('trak',box('tkhd',trak[b'tkhd'])+
        box('mdia',mdhd+hdlr+box('minf',full('smhd',b'\0'*4)+dinf+stbl)))+
        box('mvex',full('trex',u32(1)+u32(1)+u32(1024)+u32(0)+u32(0))))
    payload=b'\x21\x10\x04\x60\x8c\x1c'
    def build(offset):
        return box('moof',full('mfhd',u32(1))+box('traf',full('tfhd',u32(1),0x020000)+
            full('tfdt',struct.pack('>Q',0),0x01000000)+full('trun',u32(count)+u32(offset)+
                b''.join(u32(1024)+u32(len(payload))+u32(0x02000000) for _ in range(count)),0x701)))
    moof=build(0);return init+build(len(moof)+8)+box('mdat',payload*count)

def extras():return [('long_timestamp_fixture',long_timestamp()),('audio_timing_fixture',audio())]
