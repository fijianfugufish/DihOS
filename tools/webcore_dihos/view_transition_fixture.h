#pragma once
inline int runDihosViewTransitionFixture(WebCore::LocalFrame& frame,WebCore::Page& page,const sacx_api* api)
{
    using namespace WebCore;using namespace WTF;
    auto require=[](bool ok,const char* why){if(!ok)sacx_user_exit(192,why);};
    require(!page.settings().acceleratedCompositingEnabled(),"View transition fixture requires software rendering");
    RefPtr document=frame.document();require(!!document,"View transition document");
    require(!document->open().hasException(),"View transition document open");
    const char html[]=R"HTML(<!doctype html><style>
    @view-transition { navigation: auto; types: watch; }
    html,body{margin:0;background:white}#box{position:absolute;left:64px;top:64px;width:64px;height:64px;background:red}
    </style><div id=box></div><div id=phase>starting</div><script>
    (async()=>{
      function ok(value,why){if(!value)throw Error(why)}
      async function check(callback,expectedError,explicit=false){
        let calls=0,returned=false;
        const transition=document.startViewTransition({update:async()=>{
          ok(returned,'callback must be asynchronous');++calls;return callback();
        },types:['watch']});returned=true;
        if(explicit){transition.skipTransition();transition.skipTransition()}
        const ready=transition.ready.then(()=>{throw Error('visual ready must reject')},e=>ok(e.name==='AbortError','ready AbortError'));
        const update=transition.updateCallbackDone.then(()=>ok(!expectedError,'expected callback error'),e=>ok(e===expectedError,'update retains callback error'));
        const finished=transition.finished.then(()=>ok(!expectedError,'expected finish error'),e=>ok(e===expectedError,'finished retains callback error'));
        await Promise.all([ready,update,finished]);ok(calls===1,'callback exactly once');
        ok(document.activeViewTransition===null,'no active transition retained');
      }
      ok(typeof document.startViewTransition==='function','native transition API');
      await check(async()=>{await Promise.resolve();document.getElementById('box').style.background='#24ac68'});
      const thrown=Error('throw probe');await check(()=>{throw thrown},thrown);
      const rejected=Error('reject probe');await check(()=>Promise.reject(rejected),rejected);
      await check(()=>{},null,true);
      await check(async()=>{await check(()=>{})});
      for(let i=0;i<20;++i)await check(()=>{});
      document.getElementById('phase').textContent='done';
    })().catch(e=>document.getElementById('phase').textContent='FAIL '+e.message);
    </script>)HTML";
    FixedVector<String> chunks{String::fromUTF8(html)};
    require(!document->write(document.get(),WTF::move(chunks)).hasException(),"View transition HTML write");document->close();
    std::vector<uint32_t> pixels(640*480,0xffffffff);
    auto* surface=cairo_image_surface_create_for_data((unsigned char*)pixels.data(),CAIRO_FORMAT_ARGB32,640,480,640*4);
    require(cairo_surface_status(surface)==CAIRO_STATUS_SUCCESS,"View transition surface");
    auto deadline=MonotonicTime::now()+20_s;bool done=false;
    while(MonotonicTime::now()<deadline){
        RunLoop::cycle();page.updateRendering();page.finalizeRenderingUpdate({});
        frame.view()->updateLayoutAndStyleIfNeededRecursive();
        {GraphicsContextCairo context(surface);frame.view()->paintContents(context,IntRect(0,0,640,480));}
        cairo_surface_flush(surface);
        RefPtr phase=document->getElementById(AtomString("phase"_s));
        if(phase){auto text=phase->textContent();if(text.startsWith("FAIL"_s)){auto bytes=text.utf8();sacx_user_exit(192,bytes.data());}
            if(text=="done"_s){require(pixels[96*640+96]==0xff24ac68,"Transition callback paints real changed pixels");done=true;break;}}
        (void)sacx_user_call(SACX_USER_SLEEP_MS,4,0,0);
    }
    require(done,"View transition promises settle within deadline");
    require(std::holds_alternative<Document::SkipTransition>(document->resolveViewTransitionRule()),"Cross-document visual transitions unavailable in software");
    require(!document->activeViewTransition(),"Transition teardown leaves no active state");
    cairo_surface_destroy(surface);
    api->log("SACX software view transitions: async callbacks, errors, promises, nested/repeated skips, real pixels and cross-document opt-out OK");
    return 124;
}
