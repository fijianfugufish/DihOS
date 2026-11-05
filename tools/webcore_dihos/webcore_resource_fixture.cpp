#include "config.h"
#include "WebCoreResourceLoad.h"
#include "sacx_user_api.h"
#include "FormData.h"
#include "SessionCookiesDihOS.h"
#include "SecurityOrigin.h"
#include <JavaScriptCore/InitializeThreading.h>
#include <JavaScriptCore/Options.h>
#include <wtf/MainThread.h>
#include <algorithm>
#include <cstring>
using namespace WTF;
using namespace WebCore;
using namespace DihOSWeb;
static void check(bool value,const char* why){if(!value)sacx_user_exit(175,why);}
enum Mode { Good,Redirect,BadHeader,Unverified,HeaderOverflow,EmptyValue,Latin1Value,CookieGood,CookieMalformed,CookieUnverified,LargeResponse,ResponseOverflow,SlowResponse,StalledResponse,TrickleResponse };
static Mode mode;static bool active;static unsigned consumed,starts,closes;
static unsigned responseBytes;
static int64_t fakeTime=1000;
static int64_t start(const char* url,uint32_t bytes,uint32_t timeout){
    if(mode==SlowResponse || mode==StalledResponse || mode==TrickleResponse)check(timeout==120000,"WebCore: broker hard deadline not forwarded");
    check(!active && bytes && !memchr(url,'#',bytes),"WebCore: fragment or overlapping start");
    active=true;++starts;return 1;
}
static int64_t startFields(const char* url,uint32_t bytes,const char* fields,uint32_t fieldBytes,uint32_t timeout){
    const char expected[]="X-Test: retained\r\n";
    check(fieldBytes==sizeof(expected)-1 && !memcmp(fields,expected,fieldBytes),"WebCore: request fields changed or dropped");
    return start(url,bytes,timeout);
}
static int poll(uint64_t,sacx_user_http_info* info){
    *info={};info->state=SACX_USER_HTTP_DONE;info->http_status=mode==Redirect?302:200;
    info->available_bytes=5-consumed;info->received_bytes=5;info->tls_unverified=mode==Unverified||mode==CookieUnverified;
    if(mode==SlowResponse || mode==StalledResponse || mode==TrickleResponse){
        info->state=mode==SlowResponse && consumed==9?SACX_USER_HTTP_DONE:SACX_USER_HTTP_LOADING;
        info->received_bytes=mode==StalledResponse?3:mode==SlowResponse?std::min(consumed+3,9u):consumed+3;
        info->available_bytes=info->received_bytes-consumed;
    }
    return 0;
}
static int largePoll(uint64_t id,sacx_user_http_info* info){poll(id,info);info->received_bytes=responseBytes;info->available_bytes=responseBytes-consumed;return 0;}
static int read(uint64_t,void* out,uint32_t bytes){
    if(mode==SlowResponse || mode==StalledResponse || mode==TrickleResponse){
        if(mode==SlowResponse && consumed==9)return 0;
        if(mode==StalledResponse && consumed)return -11;
        check(bytes>=3,"WebCore: progress chunk capacity");memcpy(out,"abc",3);consumed+=3;return 3;
    }
    if(mode==LargeResponse){unsigned count=std::min(bytes,responseBytes-consumed);memset(out,'j',count);consumed+=count;return count;}
    if(consumed)return 0;memcpy(out,"hello",5);consumed=5;return 5;
}
static int close(uint64_t){check(active,"WebCore: duplicate close");active=false;++closes;return 0;}
static int64_t clockNow(){return fakeTime;}
static int headers(uint64_t,void* out,uint32_t bytes,uint32_t offset){
    if(mode==HeaderOverflow)return bytes+1;
    const char* text=mode==Redirect?"HTTP/1.1 302 Found\r\nLocation: /next\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n":
        mode==CookieGood||mode==CookieUnverified?"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nSet-Cookie: secret=token; Secure; HttpOnly; Path=/; SameSite=None\r\nSet-Cookie: public=visible; Secure; Path=/; SameSite=None\r\n\r\n":
        mode==CookieMalformed?"HTTP/1.1 200 OK\r\nSet-Cookie: secret=token; Secure; HttpOnly; Path=/; SameSite=None\r\nBad Header: value\r\n\r\n":
        mode==BadHeader?"HTTP/1.1 200 OK\r\nBad Header: value\r\n\r\n":
        mode==EmptyValue?"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nX-Test: yes\r\nx-analytics: \r\nX-Empty:\t \t\r\n\r\n":
        mode==Latin1Value?"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nX-Test: yes\r\nX-Octets: \xe9\r\n\r\n":
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nX-Test: yes\r\n\r\n";
    unsigned size=strlen(text);if(offset>size)return -22;
    unsigned count=std::min(bytes,size-offset);memcpy(out,text+offset,count);return count;
}
static ResourceTransport transport{start,poll,read,close,clockNow,headers};
static String expectedCookies;
static int64_t cookieFields(const char* url,uint32_t bytes,const char* fields,uint32_t fieldBytes,uint32_t timeout){
    auto expected=expectedCookies.utf8();
    check(fieldBytes==expected.length() && (!fieldBytes||!memcmp(fields,expected.data(),fieldBytes)),"Cookies: wrong credential mode or stale Cookie header");
    return start(url,bytes,timeout);
}
static unsigned postAttempts;
static unsigned largeAttempts;
static int64_t largePost(const char* url,uint32_t bytes,const char* fields,uint32_t fieldBytes,const void* body,uint32_t bodyBytes,uint32_t timeout){
    check(fieldBytes>2048 && fieldBytes<=16384 && bodyBytes==16384,"WebCore: large request bounds");
    check(fields[fieldBytes-2]=='\r' && fields[fieldBytes-1]=='\n',"WebCore: truncated large header");
    for(unsigned i=0;i<bodyBytes;++i)check(((const uint8_t*)body)[i]==0x5a,"WebCore: large queued body changed");
    if(!largeAttempts++)return -24;
    return start(url,bytes,timeout);
}
static int64_t postFields(const char* url,uint32_t bytes,const char* fields,uint32_t fieldBytes,const void* body,uint32_t bodyBytes,uint32_t timeout){
    const uint8_t expected[]={97,0,98,99};
    check(bodyBytes==sizeof(expected) && !memcmp(body,expected,sizeof(expected)),"WebCore: queued POST body changed");
    check(!fieldBytes,"WebCore: transport must own Content-Length");
    if(!postAttempts++)return -24;
    return start(url,bytes,timeout);
}
static void reset(Mode next){check(!active,"WebCore: leaked request");mode=next;consumed=starts=closes=0;fakeTime=1000;}
extern "C" int dihos_user_main(const sacx_api* api){
    JSC::initialize([]{JSC::Options::AllowUnfinalizedAccessScope scope;JSC::Options::useConcurrentGC()=false;});
    initializeMainThread();auto& loop=RunLoop::mainSingleton();auto queue=DispatchQueue::create(loop);
    check(cookiePublicSuffix("CO.UK"_s)&&cookiePublicSuffix("GITHUB.IO"_s)&&cookiePublicSuffix(String::fromUTF8("公司.cn")),"Cookies: noncanonical public suffix spellings must fail closed");
    auto scopedOrigin=SecurityOrigin::createFromString("https://alice.co.uk"_s);
    check(!scopedOrigin->isMatchingRegistrableDomainSuffix("CO.UK"_s,false),"Cookies: uppercase document.domain bypass");
    RunLoop::Timer watchdog(Ref{loop},"WebCore resource watchdog"_s,[]{check(false,"WebCore: watchdog");});
    auto run=[&]{watchdog.startOneShot(45_s);RunLoop::run();watchdog.stop();};
    ResourceRequest request(URL{{},"http://example.com/page#fragment"_s});
    auto isolated=request.isolatedCopy();check(isolated.url()==request.url(),"WebCore: isolated request URL");
    CertificateInfo certificate;check(certificate.isEmpty() && !certificate.summary() &&
        certificate.verificationStatus()==CertificateInfo::VerificationStatus::Unavailable,"WebCore: false TLS trust");
    for(auto next:{Good,Redirect,BadHeader,Unverified,HeaderOverflow,EmptyValue,Latin1Value}){
        reset(next);auto load=WebCoreResourceLoad::create(loop,queue,transport);
        unsigned responses=0,body=0,completions=0;bool expectedSuccess=next==Good || next==Redirect || next==EmptyValue || next==Latin1Value;
        check(!load->start(request,{
            [&](ResourceResponse&& response){
                check(expectedSuccess && response.httpStatusCode()==(next==Redirect?302:200),"WebCore: status mapping");
                check(response.mimeType()=="text/html"_s && response.textEncodingName()=="UTF-8"_s,"WebCore: MIME/charset mapping");
                check(!response.url().hasFragmentIdentifier(),"WebCore: response fragment");
                if(next==Redirect)check(response.httpHeaderField("Location"_s)=="/next"_s,"WebCore: lost redirect header");
                else check(response.httpHeaderField("X-Test"_s)=="yes"_s,"WebCore: lost custom header");
                if(next==EmptyValue)check(response.httpHeaderField("x-analytics"_s).isEmpty(),"WebCore: empty value changed");
                if(next==Latin1Value)check(response.httpHeaderField("X-Octets"_s)==String::fromLatin1("\xe9"),"WebCore: Latin-1 value changed");
                ++responses;},
            [&](std::span<const uint8_t> bytes){check(responses==1 && !completions,"WebCore: body before response");body+=bytes.size();},
            [&](ResourceError&& error){
                check(error.isNull()==expectedSuccess,"WebCore: error mapping");
                if(!expectedSuccess)check(error.errorCode()==(next==Unverified?-13:-71),"WebCore: error code");
                ++completions;loop.stop();}}),"WebCore: modeled admission");
        check(!responses && !body && !completions,"WebCore: inline callback");run();
        check(completions==1 && starts==1 && closes==1 && !active,"WebCore: completion/redirect/close ownership");
        check(body==(expectedSuccess?5u:0u),"WebCore: failed response leaked body");
    }
    api->log("WebCore response fields: empty values and Latin-1 octets OK");
    for(unsigned size:{12u*1024u*1024u,SACX_HTTP_RESPONSE_MAX,SACX_HTTP_RESPONSE_MAX+1}){
        reset(size>SACX_HTTP_RESPONSE_MAX?ResponseOverflow:LargeResponse);responseBytes=size;
        auto largeTransport=transport;largeTransport.poll=largePoll;
        auto load=WebCoreResourceLoad::create(loop,queue,largeTransport);unsigned total=0,done=0;
        check(!load->start(request,{{},[&](std::span<const uint8_t> bytes){
            check(mode==LargeResponse && !bytes.empty() && bytes.front()=='j' && bytes.back()=='j',"WebCore: large response bytes");total+=bytes.size();
        },[&](ResourceError&& error){
            check(mode==LargeResponse?(error.isNull() && total==size):(error.errorCode()==-75 && !total),"WebCore: response size boundary");
            ++done;loop.stop();}}),"WebCore: large response admission");
        run();check(done==1 && closes==1 && !active,"WebCore: large response cleanup");
    }
    api->log("WebCore response bounds: 12 MiB scripts, exact 16 MiB completion and oversized rejection OK");
    for(auto next:{SlowResponse,StalledResponse,TrickleResponse}){
        reset(next);auto load=WebCoreResourceLoad::create(loop,queue,transport);unsigned total=0,done=0;
        check(!load->start(request,{{},[&](std::span<const uint8_t> bytes){
            check(bytes.size()==3,"WebCore: progress response chunk");total+=bytes.size();
            fakeTime+=next==StalledResponse?31000:15000;
        },[&](ResourceError&& error){
            if(next==SlowResponse)check(error.isNull() && total==9 && fakeTime==46000,"WebCore: active 45-second download cancelled");
            else {
                check(error.errorCode()==-110,"WebCore: stalled response did not time out");
                if(next==TrickleResponse)check(error.localizedDescription().contains("Network total"_s) && fakeTime>=121000,"WebCore: progress bypassed hard deadline");
                else check(total==3,"WebCore: polling extended idle deadline");
            }
            ++done;loop.stop();}}),"WebCore: progress response admission");
        run();check(done==1 && closes==1 && !active,"WebCore: progress response cleanup");
    }
    api->log("WebCore response deadlines: active 45-second transfer, stalled idle timeout and hard 120-second cap OK");
    reset(Good);{
        auto withPost=transport;withPost.postWithHeaders=postFields;postAttempts=0;
        auto load=WebCoreResourceLoad::create(loop,queue,withPost);
        ResourceRequest post=request;post.setHTTPMethod("POST"_s);
        const uint8_t data[]={97,0,98,99};post.setHTTPBody(FormData::create(std::span<const uint8_t>{data,sizeof(data)}));
        post.setHTTPHeaderField("Content-Length"_s,"999"_s);
        unsigned completed=0;
        check(!load->start(post,{{},{},[&](ResourceError&& error){check(error.isNull(),"WebCore: POST failed");++completed;loop.stop();}}),"WebCore: POST admission");
        post.setHTTPBody(nullptr);run();
        check(postAttempts==2 && starts==1 && closes==1 && completed==1,"WebCore: POST retry ownership and cleanup");
    }
    api->log("WebCore POST: owned binary body, request-slot retry and kernel-owned framing OK");
    reset(Good);{
        auto largeTransport=transport;largeTransport.postWithHeaders=largePost;largeAttempts=0;
        auto load=WebCoreResourceLoad::create(loop,queue,largeTransport);
        ResourceRequest post=request;post.setHTTPMethod("POST"_s);
        Vector<uint8_t> body;body.grow(16384);std::fill(body.begin(),body.end(),0x5a);
        post.setHTTPBody(FormData::create(body.span()));
        Vector<char> value;value.grow(16000);std::fill(value.begin(),value.end(),'a');
        post.setHTTPHeaderField("X-Large"_s,String(value.span()));
        check(!load->start(post,{{},{},[&](ResourceError&& e){check(e.isNull(),"WebCore: large request failed");loop.stop();}}),"WebCore: large request admission");
        post.setHTTPBody(nullptr);post.setHTTPHeaderField("X-Large"_s,"changed"_s);run();
        check(largeAttempts==2 && starts==1 && closes==1,"WebCore: large request ownership/retry");
        auto denied=WebCoreResourceLoad::create(loop,queue,largeTransport);
        body.grow(16385);post.setHTTPBody(FormData::create(body.span()));
        check(denied->start(post,{{},{},[](auto&&){check(false,"WebCore: oversized callback");}})==-75,"WebCore: oversized body admitted");
        post.setHTTPBody(nullptr);value.grow(16384);std::fill(value.begin(),value.end(),'a');post.setHTTPHeaderField("X-Large"_s,String(value.span()));
        denied=WebCoreResourceLoad::create(loop,queue,largeTransport);
        check(denied->start(post,{{},{},[](auto&&){check(false,"WebCore: oversized header callback");}})==-75,"WebCore: oversized headers admitted");
    }
    api->log("WebCore 16 KiB requests: large headers, exact body bound, owned queue retry and oversize rejection OK");
    {
        auto withCookies=transport;withCookies.startWithHeaders=cookieFields;
        ResourceRequest r(URL{{},"https://www.example.com/page"_s});r.setIsSameSite(true);r.setAllowCookies(true);
        for(Mode next:{CookieGood,CookieMalformed,CookieUnverified}){
            clearSessionCookies();reset(next);expectedCookies=String{};
            auto load=WebCoreResourceLoad::create(loop,queue,withCookies);unsigned done=0;
            check(!load->start(r,{{},{},[&](ResourceError&& e){check(e.isNull()==(next==CookieGood),"Cookies: response validation");++done;loop.stop();}}),"Cookies: response admission");run();
            auto h=requestCookies(r,2048);check(h.has_value(),"Cookies: bounded header");
            check(next==CookieGood?(*h=="secret=token; public=visible"_s):h->isEmpty(),"Cookies: unverified/malformed response changed storage");
            check(done==1 && closes==1,"Cookies: load cleanup");
        }
        clearSessionCookies();reset(CookieGood);r.setAllowCookies(false);r.setHTTPHeaderField("Cookie"_s,"forged=must-not-leak"_s);expectedCookies=String{};
        auto omitted=WebCoreResourceLoad::create(loop,queue,withCookies);
        check(!omitted->start(r,{{},{},[&](ResourceError&& e){check(e.isNull(),"Cookies: omitted credentials load");loop.stop();}}),"Cookies: omit admission");run();
        r.setAllowCookies(true);check(requestCookies(r,2048)->isEmpty(),"Cookies: omitted credentials accepted Set-Cookie");
        clearSessionCookies();
    }
    api->log("WebCore cookies: credential omission, separate Set-Cookie fields, trusted TLS gate and malformed-header rejection OK");
    for(bool cancelInData:{false,true}){
        reset(Good);auto load=WebCoreResourceLoad::create(loop,queue,transport);unsigned completions=0;
        check(!load->start(request,{
            [&](ResourceResponse&&){if(!cancelInData){load->cancel();loop.stop();}},
            [&](auto){load->cancel();loop.stop();},
            [&](ResourceError&&){++completions;}}),"WebCore: cancel admission");run();
        check(!completions && closes==1 && !active,"WebCore: reentrant cancellation");
    }
    reset(Good);{
        auto load=WebCoreResourceLoad::create(loop,queue,transport);
        ResourceRequest post=request;post.setHTTPMethod("POST"_s);
        check(load->start(post,{{},{},[](auto&&){check(false,"WebCore: rejected callback");}})==-95 && !starts,"WebCore: unsupported method silently changed");
        load=WebCoreResourceLoad::create(loop,queue,transport);
        post=request;post.setHTTPHeaderField("X-Test"_s,"must not disappear"_s);
        check(load->start(post,{{},{},[](auto&&){check(false,"WebCore: rejected callback");}})==-95 && !starts,"WebCore: request header silently dropped");
    }
    reset(Good);{
        auto withFields=transport;withFields.startWithHeaders=startFields;
        auto load=WebCoreResourceLoad::create(loop,queue,withFields);
        ResourceRequest custom=request;custom.setHTTPHeaderField("X-Test"_s,"retained"_s);
        unsigned responses=0,body=0,done=0;
        RunLoop::Timer resume(Ref{loop},"deferred response"_s,[&]{
            check(responses==1 && !body && !done,"WebCore: body delivered during response policy deferral");
            load->setDeferred(false);
        });
        check(!load->start(custom,{
            [&](auto&&){++responses;load->setDeferred(true);resume.startOneShot(30_ms);},
            [&](auto bytes){body+=bytes.size();},
            [&](auto&& error){check(error.isNull(),"WebCore: custom GET failed");++done;loop.stop();}}),"WebCore: custom request admission");
        run();check(responses==1 && body==5 && done==1 && closes==1,"WebCore: request fields/deferred completion");
        for(const char* name:{"Host","Connection","Accept-Encoding","Content-Length"}){
            auto denied=WebCoreResourceLoad::create(loop,queue,withFields);
            ResourceRequest conflicting=request;conflicting.setHTTPHeaderField(String::fromLatin1(name),"bad"_s);
            check(denied->start(conflicting,{{},{},[](auto&&){check(false,"WebCore: denied request callback");}})==-95,"WebCore: framing override accepted");
        }
    }
    api->log("WebCore request headers: owned fields, framing rejection and response deferral OK");
    api->log("WebCore resource: real metadata, strict parsing, redirects, TLS rejection and cancellation OK");
    {
        auto load=WebCoreResourceLoad::create(loop,queue);unsigned responses=0,body=0,done=0;
        ResourceRequest live(URL{{},"http://example.com/"_s});
        live.setHTTPHeaderField("X-DihOS-Test"_s,"retained"_s);
        int result=load->start(live,{
            [&](ResourceResponse&& response){check(response.httpStatusCode()==200,"WebCore: live status");++responses;},
            [&](auto bytes){body+=bytes.size();},
            [&](ResourceError&& error){check(error.isNull(),"WebCore: live transport error");++done;loop.stop();}});
        sacx_user_info abi;check(!sacx_user_call(SACX_USER_QUERY,(uintptr_t)&abi,sizeof(abi),0),"WebCore: ABI query");
        if(!(abi.capabilities&SACX_USER_CAP_NETWORK)){
            check(result==-1 && !responses && !body && !done,"WebCore: network grant bypass");
            load->cancel();queue->close();RunLoop::cycle();api->log("SACX WebCore resource: missing network grant rejected OK");return 112;
        }
        check(!result && !done,"WebCore: live admission/inline callback");run();
        check(responses==1 && body && done==1,"WebCore: live response completion");
    }
    queue->close();RunLoop::cycle();api->log("SACX WebCore resource: native request/response, header slices and async body OK");return 111;
}
