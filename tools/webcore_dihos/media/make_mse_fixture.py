"""Owned HTML/JavaScript MSE exercise; no host or remote video playback."""
from pathlib import Path
import argparse,base64,json
from make_fragment_fixture import fragmented
from make_encoded_fixture import audio
ROOT=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path);args=p.parse_args()
OUT=(ROOT/args.output).resolve() if args.output else ROOT/'build/dihscover-mse-player'
assert OUT.is_relative_to(ROOT/'build');OUT.mkdir(exist_ok=True)
init,segments=fragmented(frames=50,duration_ms=40)
html='''<!doctype html><meta charset="utf-8"><style>html,body{margin:0;background:white}video{display:block;width:320px;height:240px;object-fit:fill}#ack{display:none}</style>
<video id="v" muted preload="auto"></video><p id="phase">starting</p><p id="ack"></p><script>
const v=document.getElementById('v'),phase=document.getElementById('phase'),ack=document.getElementById('ack');
const check=(ok,text)=>{if(!ok)throw new Error(text)};
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function until(test,text){const start=performance.now();while(!test()){check(performance.now()-start<8000,text);await delay(10)}}
function bytes(text){return Uint8Array.from(atob(text),c=>c.charCodeAt(0))}
function event(target,name){return new Promise((resolve,reject)=>{
 const timer=setTimeout(()=>finish(new Error('timeout: '+name)),8000);
 function cleanup(){clearTimeout(timer);target.removeEventListener(name,ok);target.removeEventListener('error',bad)}
 function finish(error){cleanup();error?reject(error):resolve()}
 function ok(){finish()}function bad(){finish(new Error('media error: '+(v.error?v.error.code:'unknown')))}
 target.addEventListener(name,ok);target.addEventListener('error',bad)
})}
async function append(sb,data){const done=event(sb,'updateend');sb.appendBuffer(data);await done}
async function seek(time){const done=event(v,'seeked');v.currentTime=time;await done;check(Math.abs(v.currentTime-time)<0.05,'seek clock')}
async function painted(name,next){ack.textContent='';phase.textContent=name;await until(()=>ack.textContent===next,'paint acknowledgement: '+name)}
const init=bytes(INIT),first=bytes(FIRST),second=bytes(SECOND),audioTiming=bytes(AUDIO),mime='video/mp4; codecs="avc1.42c00a"';
async function begin(){const media=new MediaSource(),url=URL.createObjectURL(media),opened=event(media,'sourceopen');v.src=url;v.load();await opened;return {media,url,sb:media.addSourceBuffer(mime)}}
async function main(){
 check(location.origin==='https://mse.dihos.test','owned HTTPS fixture origin');
 // Access alone crashed in IDBConnectionProxy with EmptyDatabaseProvider.
 phase.textContent='indexeddb';const database=indexedDB;
 check(database===window.indexedDB,'stable IndexedDB factory');
 let events=0;
 async function rejectedRequest(remove,index){
  let synchronous=true;const request=remove?database.deleteDatabase('owned-'+index):database.open('owned-'+index,1);
  const done=new Promise((resolve,reject)=>{
   const timer=setTimeout(()=>reject(new Error('IndexedDB request remained pending')),2000);
   request.onsuccess=()=>{clearTimeout(timer);reject(new Error('unavailable storage fabricated success'))};
   request.onupgradeneeded=()=>{clearTimeout(timer);reject(new Error('unavailable storage created a database'))};
   request.onerror=event=>{try{
    check(!synchronous,'IndexedDB error must be asynchronous');
    check(event.target===request && request.readyState==='done','IndexedDB error request state');
    check(request.error && request.error.name==='UnknownError','IndexedDB storage error');
    check(request.result===undefined,'failed request has no database result');events++;event.preventDefault();clearTimeout(timer);resolve();
   }catch(error){clearTimeout(timer);reject(error)}};
  });synchronous=false;await done;
 }
 await Promise.all(Array.from({length:12},(_,i)=>rejectedRequest(!!(i%2),i)));
 const names=await database.databases();check(Array.isArray(names) && names.length===0,'IndexedDB enumeration completes');
 check(events===12,'each IndexedDB request completed exactly once');await delay(0);
 check(events===12,'no duplicate IndexedDB completion');
 const child=document.createElement('iframe');document.body.appendChild(child);
 const cancelled=child.contentWindow.indexedDB.open('cancelled-context',1);
 cancelled.onerror=event=>event.preventDefault();child.remove();await delay(0);
 check(indexedDB===database,'IndexedDB remains usable after context teardown');
 console.log('SACX IndexedDB: factory, concurrent asynchronous open/delete errors and enumeration OK');
 // Keep the pre-existing complete-file backend covered with this same ABI.
 phase.textContent='file-video';const fileReady=event(v,'loadeddata');v.src=FILE_URL;v.load();await fileReady;
 check(v.videoWidth===64 && v.videoHeight===48,'complete-file dimensions');
 await seek(1.2);await seek(0);const fileEnded=event(v,'ended');await v.play();await fileEnded;
 check(v.ended,'complete-file ended');v.removeAttribute('src');v.load();
 check(typeof MediaSource==='function','native MediaSource binding');
 check(MediaSource.isTypeSupported(mime),'H264 MSE probe');
 check(MediaSource.isTypeSupported('audio/mp4; codecs="mp4a.40.2"'),'AAC timing probe');
 check(!MediaSource.isTypeSupported('video/mp4; codecs="av01.0.08M.08"'),'unsupported AV1 probe');
 check(!MediaSource.isTypeSupported('video/webm; codecs="vp9"'),'unsupported WebM probe');
 check(!MediaSource.isTypeSupported('video/mp4; codecs="avc1.gggggg"'),'malformed AVC probe');
 check(!MediaSource.isTypeSupported('video/mp4; codecs="avc1.640033"'),'untested high AVC level probe');
 phase.textContent='sourceopen';const current=await begin(),ms=current.media,sb=current.sb;
 let pos=0;for(const count of [1,7,16,53]){await append(sb,init.slice(pos,pos+count));pos+=count}await append(sb,init.slice(pos));
 check(sb.buffered.length===0,'initialization has no fabricated frames');ms.duration=2;
 await append(sb,first.slice(0,11));await append(sb,first.slice(11));
 await until(()=>v.readyState>=2,'first decoded frame');
 const canvas=document.createElement('canvas');canvas.width=1;canvas.height=1;
 const context=canvas.getContext('2d');check(!!context,'video canvas context');context.drawImage(v,0,0,1,1);
 const pixel=context.getImageData(0,0,1,1).data;check(pixel[0]>245 && pixel[1]<10 && pixel[2]<10,'origin-clean MSE canvas frame');
 check(sb.buffered.length===1 && Math.abs(sb.buffered.end(0)-1)<0.001,'first fragment range');
 await painted('red','next');await append(sb,init);sb.changeType(mime);await append(sb,init);await append(sb,second);check(Math.abs(sb.buffered.end(0)-2)<0.001,'second fragment range');ms.endOfStream();
 await seek(1.2);await painted('blue','play');await seek(0);
 const ended=event(v,'ended');phase.textContent='playing';await v.play();await ended;check(v.ended && v.currentTime>=1.99,'timed playback and ended');
 sb.timestampOffset=1;await append(sb,second);ms.endOfStream();
 check(Math.abs(v.duration-3)<0.001 && !v.ended,'reopen extends duration and clears ended');
 phase.textContent='reopening';const extended=event(v,'ended');await v.play();check(v.currentTime>=1.99,'reopen resumes instead of jumping to zero');await extended;
 check(v.currentTime>=2.99,'reopened stream reaches new end');
 phase.textContent='replacement';const replacement=await begin();await append(replacement.sb,init);replacement.media.duration=2;
 await append(replacement.sb,init.slice(0,9));replacement.sb.abort();
 await append(replacement.sb,first.slice(0,9));replacement.sb.abort();await append(replacement.sb,first);
 await until(()=>v.readyState>=2,'replacement frame after partial append abort');await painted('cancel-red','cancel');
 // A seek waiting outside the buffered range must be cancelled by teardown.
 v.currentTime=1.5;await delay(30);check(v.seeking,'seek waits for missing media');
 v.removeAttribute('src');v.load();URL.revokeObjectURL(current.url);URL.revokeObjectURL(replacement.url);
 await until(()=>replacement.media.readyState==='closed','replacement closes pending seek');
 // Separate AAC timing track, deliberately not an AAC decoding/audio test.
 // Its 87*1024/44100 endpoint is beyond the video's exact two seconds.
 phase.textContent='audio-video';const av=await begin(),asb=av.media.addSourceBuffer('audio/mp4; codecs="mp4a.40.2"');
 await append(av.sb,init);await append(asb,audioTiming);await append(av.sb,first);await append(av.sb,second);
 av.media.endOfStream();check(Math.abs(v.duration-87*1024/44100)<0.00001,'fractional AAC duration');
 await until(()=>v.readyState>=2,'separate track readiness');const avEnded=event(v,'ended');await v.play();await avEnded;
 check(v.ended && Math.abs(v.currentTime-v.duration)<0.00001,'fractional A/V endpoint reaches ended');
 v.removeAttribute('src');v.load();URL.revokeObjectURL(av.url);
 await until(()=>av.media.readyState==='closed','separate track teardown');
 phase.textContent='done';
}
main().catch(error=>{phase.textContent='FAIL: '+error.message+' state='+JSON.stringify({time:v.currentTime,duration:v.duration,paused:v.paused,ended:v.ended,seeking:v.seeking,ready:v.readyState,error:v.error?v.error.code:0})});
</script>'''
for token,data in [('INIT',init),('FIRST',segments[0]),('SECOND',segments[1]),('AUDIO',audio(87))]:html=html.replace('bytes('+token+')','bytes('+json.dumps(base64.b64encode(data).decode())+')')
html=html.replace('FILE_URL',json.dumps('data:video/mp4;base64,'+base64.b64encode(init+b''.join(segments)).decode()))
(OUT/'page_mse_fixture.html').write_text(html)
(OUT/'page_mse_fixture.js').write_text(html.partition('<script>')[2].rsplit('</script>',1)[0])
(OUT/'page_mse_fixture.inc').write_text('static const char dihos_mse_page_html[]='+json.dumps(html)+';\n')
print('Generated owned MSE append/play/seek/end/abort/replacement fixture')
