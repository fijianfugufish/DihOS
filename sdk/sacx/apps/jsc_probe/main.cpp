// Native JSC embedding and runtime-contract tests; no browser/DOM substitution.
#include "config.h"
#include "sacx_user_api.h"
#include <JavaScriptCore/JavaScript.h>
#include "InitializeThreading.h"
#include "Options.h"
// Pinned JSBase.cpp exports this debug-only synchronous full-collection hook.
extern "C" void JSSynchronousGarbageCollectForDebugging(JSContextRef);

static JSValueRef evaluate(JSGlobalContextRef context,const char *script,JSValueRef *exception){
    JSStringRef source=JSStringCreateWithUTF8CString(script);
    JSValueRef value=JSEvaluateScript(context,source,nullptr,nullptr,1,exception);
    JSStringRelease(source);return value;
}
static void check(JSGlobalContextRef context,const char *script,const char *name){
    JSValueRef exception=nullptr;
    JSValueRef value=evaluate(context,script,&exception);
    if(!value || exception || !JSValueToBoolean(context,value))sacx_user_exit(163,name);
    sacx_user_log(name);
}
static JSValueRef nativeAdd(JSContextRef context,JSObjectRef,JSObjectRef,size_t count,
                           const JSValueRef values[],JSValueRef *exception){
    if(count!=2){*exception=JSValueMakeNumber(context,-1);return JSValueMakeUndefined(context);}
    double left=JSValueToNumber(context,values[0],exception);
    if(*exception)return JSValueMakeUndefined(context);
    double right=JSValueToNumber(context,values[1],exception);
    return *exception?JSValueMakeUndefined(context):JSValueMakeNumber(context,left+right);
}
extern "C" int dihos_user_main(const sacx_api *api){
    // Sparse VM supports the upstream structure-heap reservation size without
    // acquiring that much physical RAM. Retain synchronous GC during bring-up.
    JSC::initialize([] {
        JSC::Options::AllowUnfinalizedAccessScope scope;
        // Clock reads currently cross SVC. Desktop-paced delivery can exhaust
        // concurrent marking deadlines before useful marking work runs. Use
        // the upstream synchronous scheduler until fast clock/event support
        // exists; kernel timer preemption still bounds this process's slices.
        JSC::Options::useConcurrentGC()=false;
    });
    api->log("JSC native: creating context");
    JSGlobalContextRef context=JSGlobalContextCreate(nullptr);
    if(!context)sacx_user_exit(163,"JSC context creation failed");
    JSStringRef source=JSStringCreateWithUTF8CString(
#ifdef DIHOS_JSC_SPIN_TEST
        "for(;;){}"
#else
        "var sum=0; for(var i=0;i<100;i++) sum+=i; sum;"
#endif
    );
    JSValueRef exception=nullptr;
    api->log("JSC native: evaluating script");
    JSValueRef result=JSEvaluateScript(context,source,nullptr,nullptr,1,&exception);
    if(!result || exception || JSValueToNumber(context,result,&exception)!=4950 || exception)
        sacx_user_exit(163,"JSC native arithmetic fixture failed");
#ifndef DIHOS_JSC_SPIN_TEST
    check(context,R"JS((()=>{
        class Counter { #n=40; add(v=2){this.#n+=v;return this.#n;} }
        let {value=42}={}; let next=((n)=>()=>++n)(40);
        return new Counter().add()===42 && value===42 && next()===41 && next()===42
            && ({x:{y:42}})?.x?.y===42 && (null??42)===42;
    })())JS","JSC suite: classes, closures and modern syntax OK");
    check(context,R"JS((()=>{
        let m=new Map([['a',40],['b',2]]),s=new Set([1,1,2]);
        return [...m.values()].reduce((a,b)=>a+b,0)===42 && s.size===2
            && JSON.parse(JSON.stringify({answer:[40,2]})).answer.join(',')==='40,2'
            && (2n**64n+3n)/3n===6148914691236517206n;
    })())JS","JSC suite: collections, JSON and BigInt OK");
    check(context,R"JS((()=>{
        let b=new ArrayBuffer(16),v=new DataView(b); v.setUint32(3,0x12345678,false);
        let a=new Uint8Array(b);return a[3]===0x12 && a[6]===0x78
            && v.getUint32(3,false)===0x12345678 && Uint16Array.from([21,21]).reduce((a,b)=>a+b,0)===42;
    })())JS","JSC suite: typed arrays and DataView OK");
    check(context,R"JS((()=>{
        let text='A\u{1f34e}\u00e9';
        return text.length===4 && [...text].length===3 && /\p{Letter}+/u.test('caf\u00e9')
            && 'e\u0301'.normalize('NFC')==='\u00e9'
            && new Intl.NumberFormat('en-US',{useGrouping:false}).format(1234.5)==='1234.5';
    })())JS","JSC suite: Unicode, RegExp and ICU Intl OK");
    JSStringRef functionName=JSStringCreateWithUTF8CString("nativeAdd");
    JSObjectRef function=JSObjectMakeFunctionWithCallback(context,functionName,nativeAdd);
    JSObjectSetProperty(context,JSContextGetGlobalObject(context),functionName,function,
                        kJSPropertyAttributeNone,&exception);
    JSStringRelease(functionName);
    if(exception)sacx_user_exit(163,"JSC suite: callback installation failed");
    check(context,"nativeAdd(40,2)===42 && (()=>{try{nativeAdd(1);return false;}catch(e){return e===-1;}})()",
          "JSC suite: native callbacks and exception crossing OK");
    exception=nullptr;evaluate(context,"throw new Error('owned exception')",&exception);
    if(!exception)sacx_user_exit(163,"JSC suite: missing script exception");
    JSValueProtect(context,exception);JSSynchronousGarbageCollectForDebugging(context);
    if(!JSValueIsObject(context,exception))sacx_user_exit(163,"JSC suite: exception root lost");
    JSValueUnprotect(context,exception);exception=nullptr;
    check(context,"globalThis.promiseAnswer=0;Promise.resolve(40).then(x=>{promiseAnswer=x+2});true",
          "JSC suite: promise scheduled");
    check(context,"promiseAnswer===42","JSC suite: promise microtask checkpoint OK");
    JSValueRef rooted=evaluate(context,"({answer:42, text:'rooted value'})",&exception);
    if(!rooted || exception)sacx_user_exit(163,"JSC suite: root creation failed");
    JSValueProtect(context,rooted);
    for(unsigned cycle=0;cycle<4;++cycle){
        check(context,"(()=>{let a=[];for(let i=0;i<4000;i++)a.push({i,text:'garbage-'+i});return a[3999].i===3999})()",
              "JSC suite: allocation batch OK");
        JSSynchronousGarbageCollectForDebugging(context);
        if(!JSValueIsObject(context,rooted))sacx_user_exit(163,"JSC suite: protected object lost");
    }
    JSStringRef answer=JSStringCreateWithUTF8CString("answer");
    JSValueRef property=JSObjectGetProperty(context,JSValueToObject(context,rooted,&exception),answer,&exception);
    JSStringRelease(answer);
    if(exception || JSValueToNumber(context,property,&exception)!=42 || exception)
        sacx_user_exit(163,"JSC suite: protected object content lost");
    JSValueUnprotect(context,rooted);
    api->log("JSC suite: protected roots and full GC cycles OK");
#endif
    api->log("JSC native: releasing context");
    JSStringRelease(source);JSGlobalContextRelease(context);
    // A second context must not inherit globals from the first.
    context=JSGlobalContextCreate(nullptr);
    check(context,"typeof sum==='undefined' && typeof nativeAdd==='undefined'",
          "JSC suite: context isolation OK");
    JSGlobalContextRelease(context);
    api->log("SACX JavaScriptCore: native script evaluation OK");return 87;
}
