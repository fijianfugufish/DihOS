// Actual upstream RunLoop and JSC in EL0; no substitute DOM implementation.
#include "config.h"
#include "sacx_user_api.h"
#include "DispatchQueue.h"
#include <JavaScriptCore/JavaScript.h>
#include "InitializeThreading.h"
#include "Options.h"
#include <wtf/MainThread.h>
#include <atomic>
#include <memory>
#include <thread>

using namespace WTF;
using DihOSWeb::DispatchQueue;
static void check(bool ok,const char* text){if(!ok)sacx_user_exit(166,text);}
static JSValueRef evaluate(JSGlobalContextRef context,const char* script){
    JSStringRef source=JSStringCreateWithUTF8CString(script);JSValueRef exception=nullptr;
    auto value=JSEvaluateScript(context,source,nullptr,nullptr,1,&exception);JSStringRelease(source);
    check(value && !exception,"event loop: JavaScript exception");return value;
}
struct Capture {
    RunLoop& loop;unsigned& destroyed;
    ~Capture(){check(loop.isCurrent(),"event loop: capture destroyed on wrong thread");++destroyed;}
};
extern "C" int dihos_user_main(const sacx_api* api){
    JSC::initialize([] {JSC::Options::AllowUnfinalizedAccessScope scope;JSC::Options::useConcurrentGC()=false;});
    initializeMainThread();auto& loop=RunLoop::mainSingleton();
    check(loop.isCurrent(),"event loop: main ownership");
    auto queue=DispatchQueue::create(loop);
    unsigned order=0,executed=0;
    uint64_t first=0;
    for(unsigned i=0;i<DispatchQueue::capacity;++i){
        auto result=queue->post([&,i] {check(i==++order && loop.isCurrent(),"event loop: FIFO/owner");++executed;});
        check(result.error==DispatchQueue::Error::None,"event loop: capacity admission");
        if(!i)first=result.id;
    }
    check(queue->post([]{}).error==DispatchQueue::Error::Full,"event loop: bounded backpressure");
    check(queue->cancel(first) && !queue->cancel(first),"event loop: cancellation idempotence");
    bool sentinel=false;
    loop.dispatch([&] {check(executed==DispatchQueue::turnBudget-1 && queue->pending()==24,"event loop: per-turn fairness");sentinel=true;});
    while(queue->pending())RunLoop::cycle();
    check(sentinel && executed==31 && !queue->cancel(first),"event loop: stale ID rejected");
    api->log("WebCore loop: bounded FIFO, cancellation and turn fairness OK");

    std::atomic<bool> posted=false;unsigned delivered=0;
    std::thread worker([&] {
        for(unsigned i=0;i<128;++i){
            while(true){
                auto result=queue->post([&,i] {check(loop.isCurrent() && delivered++==i,"event loop: cross-thread order");});
                if(result.error==DispatchQueue::Error::None)break;
                check(result.error==DispatchQueue::Error::Full,"event loop: producer failure");std::this_thread::yield();
            }
        }
        posted.store(true,std::memory_order_release);
    });
    while(!posted.load(std::memory_order_acquire) || queue->pending())RunLoop::cycle();
    worker.join();check(delivered==128,"event loop: cross-thread delivery count");
    unsigned destroyed=0;
    std::thread canceller([&] {
        auto result=queue->post([capture=std::unique_ptr<Capture>(new Capture {loop,destroyed})] {check(false,"event loop: cancelled callback ran");});
        check(result.id && queue->cancel(result.id),"event loop: cross-thread cancel");
    });
    canceller.join();RunLoop::cycle();check(destroyed==1,"event loop: owner disposal");
    api->log("WebCore loop: cross-thread delivery, wakeups and capture disposal OK");

    unsigned ticks=0;bool once=false;
    RunLoop::Timer cancelled(Ref {loop},"Cancelled fixture"_s,[]{check(false,"event loop: stopped timer fired");});
    RunLoop::Timer oneShot(Ref {loop},"One shot fixture"_s,[&]{once=true;});
    RunLoop::Timer repeating(Ref {loop},"Repeating fixture"_s,[&]{if(++ticks==3){repeating.stop();loop.stop();}});
    RunLoop::Timer watchdog(Ref {loop},"Watchdog fixture"_s,[]{check(false,"event loop: timer timeout");});
    cancelled.startOneShot(1_ms);cancelled.stop();oneShot.startOneShot(1_ms);
    repeating.startRepeating(2_ms);watchdog.startOneShot(2_s);RunLoop::run();watchdog.stop();
    check(once && ticks==3 && !repeating.isActive(),"event loop: timers/reentry");
    api->log("WebCore loop: one-shot, repeating, stopped timers and idle waits OK");

    JSGlobalContextRef context=JSGlobalContextCreate(nullptr);check(context,"event loop: JSC context");
    evaluate(context,"globalThis.events=[]; globalThis.onTask=()=>{events.push('task');Promise.resolve().then(()=>events.push('microtask'));};");
    JSValueRef exception=nullptr;
    auto callback=JSValueToObject(context,evaluate(context,"onTask"),&exception);check(callback && !exception,"event loop: callback type");
    JSValueProtect(context,callback);
    RunLoop::Timer scriptTimer(Ref {loop},"Script task fixture"_s,[&] {
        check(queue->post([&] {
            JSValueRef exception=nullptr;JSObjectCallAsFunction(context,callback,nullptr,0,nullptr,&exception);
            check(!exception,"event loop: callback threw");loop.stop();
        }).id,"event loop: script post");
    });
    scriptTimer.startOneShot(2_ms);watchdog.startOneShot(2_s);RunLoop::run();watchdog.stop();
    check(JSValueToNumber(context,evaluate(context,"events.join(',')==='task,microtask'?42:0"),nullptr)==42,"event loop: task/microtask order");
    JSValueUnprotect(context,callback);JSGlobalContextRelease(context);
    api->log("WebCore loop: native timer to protected JavaScript callback and promise checkpoint OK");
    for(unsigned i=0;i<100;++i){
        auto closing=DispatchQueue::create(loop);
        check(closing->post([capture=std::unique_ptr<Capture>(new Capture {loop,destroyed})]{check(false,"event loop: closed callback ran");}).id,"event loop: closing post");
        closing->close();check(closing->post([]{}).error==DispatchQueue::Error::Closed,"event loop: closed queue accepted post");
        RunLoop::cycle();
    }
    check(destroyed==101,"event loop: repeated capture cleanup");queue->close();RunLoop::cycle();
    api->log("SACX WebCore event loop: dispatch, timers, cancellation and JavaScript callbacks OK");
    return 103;
}
