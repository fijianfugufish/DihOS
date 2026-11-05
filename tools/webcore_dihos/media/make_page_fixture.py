"""Generate a deterministic self-contained page; never downloads site media."""
from pathlib import Path
import base64,json
from make_fixture import clip
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/dihscover-video-player';OUT.mkdir(exist_ok=True)
url='data:video/mp4;base64,'+base64.b64encode(clip()).decode()
html='''<!doctype html><meta charset="utf-8"><style>html,body{margin:0;background:white}video{display:block;width:320px;height:240px;object-fit:fill}#ack{display:none}</style>
<video id="v" muted preload="auto"></video><p id="phase">loading</p><p id="ack"></p>
<script>
const v=document.getElementById('v'),phase=document.getElementById('phase'),ack=document.getElementById('ack');
function fail(text){phase.textContent='FAIL: '+text;}
if(v.canPlayType('video/mp4; codecs="avc1.42C00A"')!=='probably')fail('H264 capability');
else if(v.canPlayType('video/mp4; codecs="mp4a.40.2"')!=='')fail('unsupported audio advertised');
else {
 v.onerror=()=>fail('media error '+(v.error?v.error.code:'unknown'));
 v.onloadeddata=()=>{if(phase.textContent!=='loading')return;
  if(v.videoWidth!==64||v.videoHeight!==48||v.duration!==2){fail('dimensions/duration');return;}
  v.pause();phase.textContent='seeking';v.currentTime=1;
 };
 v.onseeked=()=>{if(phase.textContent==='seeking')phase.textContent='blue';
  else if(phase.textContent==='startseek'){phase.textContent='playrequest';v.play().then(()=>phase.textContent='playing').catch(e=>fail('play '+e.name));}
 };
 v.onended=()=>{if(!v.ended||v.currentTime<1.9||v.currentTime>2.1)fail('ended/timing');else phase.textContent='done';};
 setInterval(()=>{if(ack.textContent==='play'){ack.textContent='';phase.textContent='startseek';v.currentTime=0;}},5);
 v.src=VIDEO_URL;
}
</script>'''.replace('VIDEO_URL',json.dumps(url))
(OUT/'page_video_fixture.html').write_text(html)
(OUT/'page_video_fixture.inc').write_text('static constexpr char dihos_video_page_html[]='+json.dumps(html)+';\n')
print('Generated self-contained HTML video playback fixture',flush=True)
