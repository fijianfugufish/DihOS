"""Apply narrow media edits to the isolated native engine, never the baseline."""
from pathlib import Path
import hashlib
def stage(original,source,root):
    changed={}
    def write(name,text):
        target=source/name
        if not target.exists() or target.read_text()!=text:target.write_text(text)
        changed[name]=hashlib.sha256(target.read_bytes()).hexdigest()
    def patch(name,before,after):
        text=(original/name).read_text();assert text.count(before)==1,name
        write(name,text.replace(before,after))
    patch('Source/WebCore/platform/graphics/MediaPlayer.h',
          '    WirelessPlayback,\n    Remote\n};',
          '    WirelessPlayback,\n    Remote,\n    DihOS\n};')
    patch('Source/WebCore/platform/graphics/MediaPlayerEnums.h',
          '    CocoaWebM,\n    WirelessPlayback,\n};',
          '    CocoaWebM,\n    WirelessPlayback,\n    DihOS,\n};')
    name='Source/WebCore/platform/graphics/MediaPlayer.cpp'
    text=(original/name).read_text()
    before='#include "MediaPlayerPrivate.h"';assert text.count(before)==1
    text=text.replace(before,before+'\n#if PLATFORM(DIHOS)\n#include "MediaPlayerDihOS.h"\n#endif')
    before='    ASSERT(mediaEngineVectorLock.isLocked());\n\n#if USE(AVFOUNDATION)';assert text.count(before)==1
    text=text.replace(before,'    ASSERT(mediaEngineVectorLock.isLocked());\n\n#if PLATFORM(DIHOS)\n    registerDihOSMediaEngine(addMediaEngine);\n#endif\n\n#if USE(AVFOUNDATION)')
    write(name,text)
    name='Source/WebCore/PlatformDihOS.cmake'
    write(name,(original/name).read_text()+'\nlist(APPEND WebCore_SOURCES platform/dihos/MediaPlayerDihOS.cpp)\n')
    name='Source/WebCore/PAL/pal/PlatformDihOS.cmake'
    write(name,(original/name).read_text()+'\nlist(APPEND PAL_SOURCES system/ClockGeneric.cpp)\n')
    for name in ['MediaPlayerDihOS.cpp','MediaPlayerDihOS.h','memory_video.h']:
        write('Source/WebCore/platform/dihos/'+name,(root/'tools/webcore_dihos/media'/name).read_text())
    return changed
