#include "config.h"
#include "ResourceLoad.h"
#include "sacx_user_api.h"
#include <JavaScriptCore/JavaScript.h>
#include "InitializeThreading.h"
#include "Options.h"
#include <wtf/MainThread.h>
#include <memory>
#include <algorithm>
#include <cstring>
using namespace WTF;
using namespace DihOSWeb;
static void check(bool ok,const char* message){if(!ok)sacx_user_exit(168,message);}
static JSValueRef eval(JSGlobalContextRef context,const char* script){
    auto text=JSStringCreateWithUTF8CString(script);JSValueRef exception=nullptr;
    auto value=JSEvaluateScript(context,text,nullptr,nullptr,1,&exception);JSStringRelease(text);
    check(value && !exception,"resource: JavaScript exception");return value;
}
enum Mode {Good,Pending,Failed,Truncated,Unverified,Oversized,EarlyEOF,CloseFailed,Large,OverLimit};
static Mode mode;static bool active,badHeaderCount;static unsigned cursor,closed,largeBytes=1024*1024;static int64_t fakeNow;
static unsigned busyAttempts;
static bool checkOwned;
static int64_t fakeStart(const char* url,uint32_t bytes,uint32_t){
    if(busyAttempts){--busyAttempts;return -24;}
    if(checkOwned)check(bytes==19 && !memcmp(url,"http://example.com/",19),"resource: queued URL not owned");
    check(!active,"resource: overlapping modeled requests");active=true;return 1;
}
static int fakePoll(uint64_t id,sacx_user_http_info* info){
    check(active && id==1,"resource: modeled poll handle");*info={};info->state=SACX_USER_HTTP_DONE;info->http_status=200;
    info->received_bytes=9;info->available_bytes=9-cursor;
    if(mode==Large){info->received_bytes=largeBytes;info->available_bytes=info->received_bytes-cursor;}
    if(mode==OverLimit){info->received_bytes=SACX_HTTP_RESPONSE_MAX+1;info->available_bytes=info->received_bytes;}
    if(mode==Pending){*info={};info->state=SACX_USER_HTTP_LOADING;}
    if(mode==Failed)info->state=SACX_USER_HTTP_FAILED;
    if(mode==Truncated)info->truncated=1;
    if(mode==Unverified)info->tls_unverified=1;
    if(mode==EarlyEOF)info->state=SACX_USER_HTTP_LOADING;
    return 0;
}
static int fakeRead(uint64_t id,void* out,uint32_t bytes){
    check(active && id==1 && bytes>=3,"resource: modeled read handle");
    if(mode==Oversized)return bytes+1;
    if(mode==EarlyEOF)return 0;
    if(mode==Pending)return -11;
    if(mode==Large){unsigned count=std::min<unsigned>(bytes,largeBytes-cursor);memset(out,'x',count);cursor+=count;return count;}
    if(cursor==9)return 0;
    auto buffer=static_cast<uint8_t*>(out);for(unsigned i=0;i<3;++i)buffer[i]=static_cast<uint8_t>('a'+cursor+i);
    cursor+=3;return 3;
}
static int fakeClose(uint64_t id){check(active && id==1,"resource: close once");active=false;++closed;return mode==CloseFailed?-5:0;}
static int64_t fakeClock(){return fakeNow;}
static int fakeHeaders(uint64_t id,void* out,uint32_t bytes,uint32_t offset){
    check(active && id==1,"resource: modeled header handle");
    if(badHeaderCount)return bytes+1;
    static const char headers[]="HTTP/1.1 200 OK\r\n\r\n";
    if(offset>sizeof(headers)-1)return -22;
    unsigned count=std::min<unsigned>(bytes,sizeof(headers)-1-offset);
    memcpy(out,headers+offset,count);return count;
}
static ResourceTransport transport {fakeStart,fakePoll,fakeRead,fakeClose,fakeClock,fakeHeaders};
static void reset(Mode next=Good){check(!active,"resource: leaked modeled request");mode=next;cursor=closed=busyAttempts=0;fakeNow=1000;badHeaderCount=checkOwned=false;}
static constexpr char url[]="http://example.com/";

extern "C" int dihos_user_main(const sacx_api* api){
    JSC::initialize([] {JSC::Options::AllowUnfinalizedAccessScope scope;JSC::Options::useConcurrentGC()=false;});
    initializeMainThread();auto& loop=RunLoop::mainSingleton();auto queue=DispatchQueue::create(loop);
    // Denied launch is a useful guest permission test, not a live HTTP pass.
    sacx_user_info abi;check(!sacx_user_call(SACX_USER_QUERY,(uintptr_t)&abi,sizeof(abi),0),"resource: ABI query");
    if(!(abi.capabilities&SACX_USER_CAP_NETWORK)){
        auto denied=ResourceLoad::create(loop,queue);unsigned callbacks=0;
        check(denied->start(url,sizeof(url)-1,{{},{},[&](int){++callbacks;}})==-1 && !callbacks,"resource: grant bypass");
        denied->cancel();queue->close();RunLoop::cycle();
        api->log("SACX resource loop: network denied without explicit grant OK");return 108;
    }
    auto context=JSGlobalContextCreate(nullptr);check(context,"resource: context creation");
    eval(context,"globalThis.bytes=0;globalThis.chunks=0;globalThis.micro=0;globalThis.onChunk=n=>{bytes+=n;chunks++;Promise.resolve().then(()=>micro++);};");
    JSValueRef exception=nullptr;auto callback=JSValueToObject(context,eval(context,"onChunk"),&exception);
    check(callback && !exception,"resource: callback type");JSValueProtect(context,callback);
    auto jsChunk=[&](unsigned n){JSValueRef value=JSValueMakeNumber(context,n),error=nullptr;
        JSObjectCallAsFunction(context,callback,nullptr,1,&value,&error);check(!error,"resource: chunk callback exception");};
    RunLoop::Timer watchdog(Ref {loop},"Resource fixture watchdog"_s,[]{check(false,"resource: fixture watchdog");});
    auto run=[&]{watchdog.startOneShot(45_s);RunLoop::run();watchdog.stop();};
    reset();unsigned headers=0,body=0,done=0;
    {
        auto load=ResourceLoad::create(loop,queue,transport);
        check(!load->start(url,sizeof(url)-1,{
            [&](const auto& info){
                check(loop.isCurrent() && !body && !done && info.http_status==200,"resource: header order");++headers;
                char raw[32];check(load->copyHeaders(raw,7,0)==7 && !memcmp(raw,"HTTP/1.",7),"resource: header slice");
                check(load->copyHeaders(raw,1,19)==0,"resource: header EOF");
                check(load->copyHeaders(raw,0,0)==-22 && load->copyHeaders(raw,4097,0)==-22,"resource: header bounds");
                badHeaderCount=true;check(load->copyHeaders(raw,7,0)==-71,"resource: excessive header count");badHeaderCount=false;
            },
            [&](const uint8_t*,unsigned n){check(headers==1 && !done && loop.isCurrent(),"resource: body order/owner");body+=n;jsChunk(n);},
            [&](int error){check(!error && body==9 && closed==1,"resource: complete after close");++done;loop.stop();}}),"resource: modeled start");
        check(!headers && !body && !done,"resource: start executed callbacks inline");
        char raw[8];check(load->copyHeaders(raw,sizeof(raw),0)==-9,"resource: premature header read");run();
        check(load->copyHeaders(raw,sizeof(raw),0)==-9,"resource: headers after close");
        check(load->state()==ResourceLoad::State::Finished && done==1 && load->received()==9,"resource: terminal state");
    }
    check(JSValueToNumber(context,eval(context,"bytes===9 && chunks===3 && micro===3 ?42:0"),nullptr)==42,"resource: promise checkpoints");
    api->log("Resource client: headers, chunks, completion and JavaScript promise ordering OK");

    for(unsigned i=0;i<100;++i){
        reset();auto load=ResourceLoad::create(loop,queue,transport);
        check(!load->start(url,sizeof(url)-1,{{},{},[](int){check(false,"resource: cancelled callback");}}),"resource: cancel admission");
        load->cancel();load->cancel();RunLoop::cycle();check(!active && closed==1,"resource: idempotent cancel/close");
    }
    for(bool insideData:{false,true}){
        reset();auto load=ResourceLoad::create(loop,queue,transport);unsigned completions=0;
        ResourceLoad::Callbacks callbacks;
        if(insideData)callbacks.data=[&,guard=std::make_unique<unsigned>(42)](const uint8_t*,unsigned){load->cancel();check(*guard==42,"resource: current data capture destroyed");};
        else callbacks.headers=[&,guard=std::make_unique<unsigned>(42)](const auto&){load->cancel();check(*guard==42,"resource: current header capture destroyed");};
        callbacks.complete=[&](int){++completions;};
        check(!load->start(url,sizeof(url)-1,WTF::move(callbacks)),"resource: reentrant cancellation admission");
        RunLoop::cycle();check(load->state()==ResourceLoad::State::Cancelled && !completions && closed==1,"resource: reentrant cancellation");
    }
    reset();{auto load=ResourceLoad::create(loop,queue,transport);check(!load->start(url,sizeof(url)-1,{{},{},[](int){check(false,"resource: callback after destruction");}}),"resource: destruction admission");}
    RunLoop::cycle();check(closed==1 && !active,"resource: weak callback lifetime");
    api->log("Resource client: cancellation, reentrant captures and destruction cleanup OK");

    for(auto scenario:{Pending,Failed,Truncated,Unverified,Oversized,EarlyEOF,CloseFailed}){
        reset(scenario);auto load=ResourceLoad::create(loop,queue,transport);unsigned completions=0;
        int expected=scenario==Pending?-110:scenario==Truncated?-75:scenario==Unverified?-13:
            scenario==Oversized || scenario==EarlyEOF?-71:-5;
        check(!load->start(url,sizeof(url)-1,{{},{},[&](int error){check(error==expected,"resource: wrong failure");++completions;loop.stop();}},1000),"resource: error admission");
        if(scenario==Pending)fakeNow=2001;run();
        check(completions==1 && closed==1 && !active && load->state()==ResourceLoad::State::Failed,"resource: error completion/cleanup");
    }
    reset(Pending);{
        auto load=ResourceLoad::create(loop,queue,transport);unsigned completed=0;
        load->setDeferred(true);
        check(!load->start(url,sizeof(url)-1,{
            [](auto&){check(false,"resource: deferred headers delivered");},
            [](auto*,unsigned){check(false,"resource: deferred body delivered");},
            [&](int error){check(error==-110,"resource: deferred request lost deadline");++completed;loop.stop();}},1000),"resource: deferred admission");
        fakeNow=2001;run();check(completed==1 && closed==1 && !active,"resource: deferred timeout cleanup");
    }
    reset();for(unsigned i=0;i<DispatchQueue::capacity;++i)check(queue->post([]{}).id,"resource: fill queue");
    {auto load=ResourceLoad::create(loop,queue,transport);check(!load->start(url,sizeof(url)-1,{{},{},[&](int error){check(!error,"resource: backpressure failure");loop.stop();}}),"resource: full queue admission");run();check(closed==1,"resource: backpressure cleanup");}
    reset();{
        auto closing=DispatchQueue::create(loop);auto load=ResourceLoad::create(loop,closing,transport);
        check(!load->start(url,sizeof(url)-1,{{},{},[&](int error){check(error==-125,"resource: queue close error");loop.stop();}}),"resource: queue close admission");
        closing->close();run();check(closed==1,"resource: closed queue cleanup");
    }
    api->log("Resource client: deadlines, transport/protocol errors, TLS rejection and queue backpressure OK");

    reset();{
        auto load=ResourceLoad::create(loop,queue,transport);unsigned headers=0,body=0;
        check(!load->start(url,sizeof(url)-1,{
            [&](const auto&){++headers;load->setDeferred(true);},
            [&](const uint8_t*,unsigned n){body+=n;fakeNow+=15000;},
            [&](int error){check(!error && body==9,"resource: downloaded body timed out while consuming");loop.stop();}},1000),"resource: slow consumer admission");
        RunLoop::cycle();check(headers==1 && !body,"resource: deferred response policy");
        fakeNow=4000;load->setDeferred(false);run();check(closed==1,"resource: slow consumer cleanup");
    }
    reset();{
        auto load=ResourceLoad::create(loop,queue,transport);
        check(!load->start(url,sizeof(url)-1,{
            [&](const auto&){load->setDeferred(true);},{},
            [&](int error){check(error==-110 && !memcmp(load->metadata().error,"Browser response processing",27),"resource: stalled consumer diagnosis");loop.stop();}},1000),"resource: consumer watchdog admission");
        RunLoop::cycle();fakeNow=121001;run();check(closed==1 && !active,"resource: consumer watchdog cleanup");
    }
    api->log("Resource client: completed downloads survive slow/deferred consumers; stalled consumer watchdog and timeout phases OK");

    for(unsigned size:{1024u*1024u,12u*1024u*1024u,SACX_HTTP_RESPONSE_MAX}){reset(Large);largeBytes=size;
        auto load=ResourceLoad::create(loop,queue,transport);unsigned total=0;
        check(!load->start(url,sizeof(url)-1,{{},[&](const uint8_t* bytes,unsigned n){check(bytes[0]=='x',"resource: large data");total+=n;},
            [&](int error){check(!error && total==largeBytes,"resource: large body truncated");loop.stop();}}),"resource: large admission");
        run();check(closed==1,"resource: large close");
    }
    reset(OverLimit);{
        auto load=ResourceLoad::create(loop,queue,transport);
        check(!load->start(url,sizeof(url)-1,{{},{},[&](int error){check(error==-75,"resource: oversized metadata accepted");loop.stop();}}),"resource: limit admission");run();
    }
    reset();busyAttempts=2;checkOwned=true;{
        char ownedURL[]="http://example.com/";
        auto load=ResourceLoad::create(loop,queue,transport);
        check(!load->start(ownedURL,sizeof(ownedURL)-1,{{},{},[&](int error){check(!error,"resource: quota became load failure");loop.stop();}}),"resource: quota queue admission");
        memset(ownedURL,'?',sizeof(ownedURL)-1);run();check(closed==1 && !busyAttempts,"resource: quota retry and ownership");
    }
    reset();busyAttempts=1000;{
        auto load=ResourceLoad::create(loop,queue,transport);
        check(!load->start(url,sizeof(url)-1,{{},{},[&](int error){check(error==-110,"resource: queued deadline extended");loop.stop();}},1000),"resource: queued timeout admission");
        fakeNow=2001;run();check(!closed && !active,"resource: unadmitted handle closed");
    }
    reset();busyAttempts=1000;{
        Vector<Ref<ResourceLoad>> waiting;
        for(unsigned i=0;i<64;++i){auto load=ResourceLoad::create(loop,queue,transport);
            check(!load->start(url,sizeof(url)-1,{{},{},[](int){check(false,"resource: queued cancellation callback");}}),"resource: waiting capacity");waiting.append(WTF::move(load));}
        auto overflow=ResourceLoad::create(loop,queue,transport);
        check(overflow->start(url,sizeof(url)-1,{{},{},[](int){}})==-24,"resource: unbounded waiting queue");
        for(auto& load:waiting)load->cancel();RunLoop::cycle();check(!active && !closed,"resource: pending cancellation touched transport");
    }
    reset();
    api->log("Resource client: MiB bodies, response bounds, owned quota retries, queued deadlines and cancellation OK");

    eval(context,"bytes=chunks=micro=0;");headers=body=done=0;
    {
        auto load=ResourceLoad::create(loop,queue);
        // Explicit diagnostic exception for the existing unauthenticated TLS
        // transport. The default client policy above was tested to reject it.
        check(!load->start(url,sizeof(url)-1,{
            [&](const auto& info){check(info.http_status>=200 && info.http_status<300,"resource: live HTTP status");++headers;},
            [&](const uint8_t*,unsigned n){check(headers==1 && !done,"resource: live ordering");body+=n;jsChunk(n);},
            [&](int error){if(error){sacx_user_log(load->metadata().error);sacx_user_exit(168,"resource: native HTTP failed");}++done;loop.stop();}},30000,true),"resource: native HTTP admission");
        check(!done && !body,"resource: live synchronous callback");run();
        check(headers==1 && body && done==1 && load->state()==ResourceLoad::State::Finished,"resource: live completion");
    }
    check(JSValueToNumber(context,eval(context,"bytes>0 && micro===chunks?42:0"),nullptr)==42,"resource: native promise checkpoint");
    JSValueUnprotect(context,callback);JSGlobalContextRelease(context);queue->close();RunLoop::cycle();
    api->log("SACX resource loop: async HTTP headers/body/completion and cancellation OK");return 106;
}
