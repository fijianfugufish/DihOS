#include "config.h"
#include "ResourceLoad.h"
#include "../../sdk/sacx/include/sacx_user_http_request.h"
#include <cstdio>

namespace DihOSWeb {
using namespace WTF;
static constexpr unsigned responseLimit=SACX_HTTP_RESPONSE_MAX;
static constexpr unsigned networkTotalMs=120000;
// Accessed only on the owning main RunLoop. Bound waiting URL/header copies,
// without turning the kernel's per-process slot quota into resource failures.
static unsigned waitingLoads;
static ResourceLoad* diagnosticHead;
static constexpr unsigned waitingLimit=64;
static int64_t nativeNow(){return sacx_user_call(SACX_USER_MONOTONIC_MS,0,0,0);}
static int64_t nativePost(const char* url,uint32_t bytes,const char* headers,uint32_t headerBytes,const void* body,uint32_t bodyBytes,uint32_t timeout){
    return sacx_user_http_send_request(url,bytes,headers,headerBytes,body,bodyBytes,SACX_USER_HTTP_POST,timeout);
}
WTF::Ref<ResourceLoad> ResourceLoad::create(WTF::RunLoop& loop,DispatchQueue& queue){
    return create(loop,queue,{sacx_user_http_start,sacx_user_http_poll,sacx_user_http_read,sacx_user_http_close,nativeNow});
}
WTF::Ref<ResourceLoad> ResourceLoad::createManual(WTF::RunLoop& loop,DispatchQueue& queue){
    return create(loop,queue,{sacx_user_http_start_manual,sacx_user_http_poll,sacx_user_http_read,sacx_user_http_close,nativeNow,sacx_user_http_headers,sacx_user_http_start_request,nativePost});
}
int ResourceLoad::copyHeaders(void* out,unsigned bytes,unsigned offset){
    RELEASE_ASSERT(m_loop->isCurrent());
    if(m_state!=State::Loading || !m_headersSent || !m_request)return -9;
    if(!out || !bytes || bytes>SACX_USER_HTTP_READ_MAX || offset>SACX_USER_HTTP_HEADERS_MAX)return -22;
    if(!m_transport.headers)return -95;
    int rc=m_transport.headers(m_request,out,bytes,offset);
    return rc>static_cast<int>(bytes)?-71:rc;
}
WTF::Ref<ResourceLoad> ResourceLoad::create(WTF::RunLoop& loop,DispatchQueue& queue,ResourceTransport transport){
    return WTF::adoptRef(*new ResourceLoad(loop,queue,transport));
}
ResourceLoad::ResourceLoad(WTF::RunLoop& loop,DispatchQueue& queue,ResourceTransport transport)
    :m_loop(loop),m_queue(queue),m_transport(transport),m_timer(WTF::Ref {loop},"DihOS resource poll"_s,[this]{tick();})
{
    RELEASE_ASSERT(loop.isCurrent());
    RELEASE_ASSERT(transport.start && transport.poll && transport.read && transport.close && transport.now);
    m_next=diagnosticHead;if(m_next)m_next->m_previous=this;diagnosticHead=this;
}
int ResourceLoad::start(const char* url,unsigned bytes,Callbacks&& callbacks,unsigned timeoutMs,bool allowUnverified){
    return startInternal(url,bytes,nullptr,0,WTF::move(callbacks),timeoutMs,allowUnverified);
}
int ResourceLoad::startWithHeaders(const char* url,unsigned bytes,const char* headers,unsigned headerBytes,Callbacks&& callbacks,unsigned timeoutMs){
    RELEASE_ASSERT(m_loop->isCurrent());
    if(m_state!=State::Idle)return -16;
    if(headerBytes && !m_transport.startWithHeaders)return -95;
    return startInternal(url,bytes,headers,headerBytes,WTF::move(callbacks),timeoutMs,false);
}
int ResourceLoad::startPost(const char* url,unsigned bytes,const char* headers,unsigned headerBytes,const uint8_t* body,unsigned bodyBytes,Callbacks&& callbacks,unsigned timeoutMs){
    if(bodyBytes>SACX_USER_HTTP_BODY_MAX || (bodyBytes&&!body))return -75;
    return startInternal(url,bytes,headers,headerBytes,WTF::move(callbacks),timeoutMs,false,body,bodyBytes,true);
}
int ResourceLoad::startInternal(const char* url,unsigned bytes,const char* headers,unsigned headerBytes,Callbacks&& callbacks,unsigned timeoutMs,bool allowUnverified,const uint8_t* body,unsigned bodyBytes,bool post){
    RELEASE_ASSERT(m_loop->isCurrent());
    if(m_state!=State::Idle)return -16;
    if(post && !m_transport.postWithHeaders)return -95;
    if(bytes>SACX_USER_HTTP_URL_MAX)return -36;
    if(!callbacks.complete || !url || !bytes || timeoutMs<1000 || timeoutMs>120000 ||
       headerBytes>SACX_USER_HTTP_REQUEST_HEADERS_MAX || (headerBytes&&!headers))return -22;
    if(m_queue->isClosed())return -125;
    unsigned label=0;for(;label<bytes && label+1<sizeof(m_diagnosticURL);++label){
        unsigned char c=url[label];if(c=='?' || c=='#')break;m_diagnosticURL[label]=c<32 || c==127?' ':c;
    }m_diagnosticURL[label]=0;
    int64_t now=m_transport.now();if(now<0)return static_cast<int>(now);
    // The broker owns a hard total deadline. Our shorter deadline measures
    // inactivity, and must not cancel a large response which is progressing.
    int64_t request=post?m_transport.postWithHeaders(url,bytes,headers,headerBytes,body,bodyBytes,networkTotalMs):headerBytes?m_transport.startWithHeaders(url,bytes,headers,headerBytes,networkTotalMs):m_transport.start(url,bytes,networkTotalMs);
    if(request<=0 && request!=-24)return request?static_cast<int>(request):-71;
    if(request==-24){
        if(waitingLoads>=waitingLimit)return -24;
        m_queuedURL.append(std::span{url,bytes});
        if(headerBytes)m_queuedHeaders.append(std::span{headers,headerBytes});
        if(bodyBytes)m_queuedBody.append(std::span{body,bodyBytes});
        m_waitingForSlot=true;++waitingLoads;
    }else m_request=static_cast<uint64_t>(request);
    m_post=post;
    m_deadline=static_cast<uint64_t>(now)+timeoutMs;
    m_idleTimeoutMs=timeoutMs;if(m_request)m_totalDeadline=static_cast<uint64_t>(now)+networkTotalMs;
    m_callbacks=WTF::move(callbacks);m_allowUnverified=allowUnverified;m_state=State::Loading;
    // Timer also observes queue closure while a queued delivery is discarded.
    m_timer.startRepeating(WTF::Seconds::fromMilliseconds(10));
    int rc=schedule();
    if(rc){m_state=State::Failed;m_error=rc;dispose();m_callbacks={};}
    return rc;
}
int ResourceLoad::admitQueued(uint64_t now){
    if(!m_waitingForSlot)return 0;
    auto remaining=static_cast<unsigned>(m_deadline-now);
    if(remaining<1000)return -110;
    auto request=m_post?m_transport.postWithHeaders(m_queuedURL.span().data(),m_queuedURL.size(),m_queuedHeaders.span().data(),m_queuedHeaders.size(),m_queuedBody.span().data(),m_queuedBody.size(),networkTotalMs):m_queuedHeaders.isEmpty()?
        m_transport.start(m_queuedURL.span().data(),m_queuedURL.size(),networkTotalMs):
        m_transport.startWithHeaders(m_queuedURL.span().data(),m_queuedURL.size(),m_queuedHeaders.span().data(),m_queuedHeaders.size(),networkTotalMs);
    if(request==-24)return -11;
    if(request<=0)return request?static_cast<int>(request):-71;
    m_request=request;m_waitingForSlot=false;--waitingLoads;
    m_totalDeadline=now+networkTotalMs;m_deadline=now+m_idleTimeoutMs;
    m_queuedURL.clear();m_queuedHeaders.clear();m_queuedBody.clear();return 0;
}
int ResourceLoad::schedule(){
    if(m_queue->isClosed())return -125;
    if(m_pending || m_state!=State::Loading || m_deferred)return 0;
    auto posted=m_queue->post([weakThis=WTF::WeakPtr { *this }] {if(weakThis)weakThis->deliver();});
    if(posted.error==DispatchQueue::Error::Full)return 0; // Periodic retry, no busy spin.
    if(posted.error!=DispatchQueue::Error::None)return -125;
    m_pending=posted.id;return 0;
}
void ResourceLoad::tick(){
    WTF::Ref protectedThis { *this };
    if(m_state!=State::Loading)return;
    int64_t now=m_transport.now();
    if(now<0){finish(static_cast<int>(now));return;}
    if(int error=updateTransport(static_cast<uint64_t>(now))){finish(error);return;}
    if(int rc=schedule())finish(rc);
}
int ResourceLoad::timedOut(const char* phase){
    // Retain the phase and byte counts before closing the request, so the
    // visible error distinguishes network waiting from a paused consumer.
    std::snprintf(m_info.error,sizeof(m_info.error),"%s timeout: received=%llu, delivered=%llu, deferred=%u",
        phase,static_cast<unsigned long long>(m_info.received_bytes),static_cast<unsigned long long>(m_received),m_deferred?1u:0u);
    return -110;
}
int ResourceLoad::updateTransport(uint64_t now){
    if(!m_request)return now>=m_deadline?timedOut("Request slot"):0;
    int rc=m_transport.poll(m_request,&m_info);if(rc)return rc;
    if(m_info.state<SACX_USER_HTTP_QUEUED || m_info.state>SACX_USER_HTTP_CANCELLED ||
       m_info.available_bytes>m_info.received_bytes || m_info.received_bytes<m_networkBytes || m_info.http_status>999)return -71;
    if(m_info.received_bytes>responseLimit)return -75;
    if(m_info.tls_unverified && !m_allowUnverified)return -13;
    if(m_info.truncated)return -75;
    if(m_info.state==SACX_USER_HTTP_FAILED || m_info.state==SACX_USER_HTTP_CANCELLED)return -5;
    if(m_info.state==SACX_USER_HTTP_DONE){
        // The transport owns its network deadline. Once it has completed,
        // remaining work is response policy, parsing and bounded body copies.
        // Give that consumer a separate bounded watchdog, including deferral.
        if(!m_deliveryDeadline)m_deliveryDeadline=now+120000;
        return now>=m_deliveryDeadline?timedOut("Browser response processing"):0;
    }
    if(now>=m_totalDeadline)return timedOut("Network total");
    if(m_info.received_bytes>m_networkBytes || (!m_responseObserved && m_info.http_status))
        m_deadline=now+m_idleTimeoutMs;
    m_networkBytes=m_info.received_bytes;m_responseObserved|=!!m_info.http_status;
    return now>=m_deadline?timedOut(m_info.state==SACX_USER_HTTP_QUEUED?"Network queue":"Network response idle"):0;
}
void ResourceLoad::deliver(){
    RELEASE_ASSERT(m_loop->isCurrent());WTF::Ref protectedThis { *this };m_pending=0;
    if(m_state!=State::Loading || m_deferred)return;
    int64_t now=m_transport.now();
    if(now<0){finish(static_cast<int>(now));return;}
    if(m_waitingForSlot && static_cast<uint64_t>(now)>=m_deadline){finish(timedOut("Request slot"));return;}
    if(int admitted=admitQueued(static_cast<uint64_t>(now))){if(admitted!=-11)finish(admitted);return;}
    if(int error=updateTransport(static_cast<uint64_t>(now))){finish(error);return;}
    if(!m_headersSent && m_info.http_status){
        m_headersSent=true;auto callback=WTF::move(m_callbacks.headers);
        if(callback)callback(m_info);
        if(m_state!=State::Loading || m_deferred)return;
    }
    if(m_info.state==SACX_USER_HTTP_DONE && !m_headersSent){finish(-71);return;}
    if(!m_headersSent){if(m_info.available_bytes)finish(-71);return;}
    int rc=m_transport.read(m_request,m_chunk,sizeof(m_chunk));
    if(rc>static_cast<int>(sizeof(m_chunk))){finish(-71);return;}
    if(rc>0){
        if(m_received+static_cast<unsigned>(rc)>responseLimit){finish(-75);return;}
        m_received+=static_cast<unsigned>(rc);auto callback=WTF::move(m_callbacks.data);
        if(callback)callback(m_chunk,static_cast<unsigned>(rc));
        if(m_state!=State::Loading)return;
        m_callbacks.data=WTF::move(callback);
        if(int error=schedule())finish(error);
        return;
    }
    if(rc==-11)return;
    if(!rc && (m_info.state!=SACX_USER_HTTP_DONE || !m_headersSent)){finish(-71);return;}
    finish(rc);
}
int ResourceLoad::dispose(){
    m_timer.stop();
    if(m_waitingForSlot){m_waitingForSlot=false;--waitingLoads;}
    m_queuedURL.clear();m_queuedHeaders.clear();m_queuedBody.clear();
    if(m_pending){m_queue->cancel(m_pending);m_pending=0;}
    if(m_request){auto request=m_request;m_request=0;return m_transport.close(request);}
    return 0;
}
void ResourceLoad::finish(int error){
    if(m_state!=State::Loading)return;
    m_error=error;m_state=error?State::Failed:State::Finished;
    int closed=dispose();if(!error && closed){error=closed;m_error=closed;m_state=State::Failed;}
    auto complete=WTF::move(m_callbacks.complete);m_callbacks={};
    if(complete)complete(error);
}
void ResourceLoad::cancel(){
    RELEASE_ASSERT(m_loop->isCurrent());
    if(m_state==State::Loading || m_state==State::Idle)m_state=State::Cancelled;
    dispose();m_callbacks={};
}
void ResourceLoad::setDeferred(bool deferred){
    RELEASE_ASSERT(m_loop->isCurrent());
    m_deferred=deferred;
    if(!deferred && m_state==State::Loading)if(int error=schedule())finish(error);
}
const char* ResourceLoad::diagnostics(){
    static char line[384];unsigned active=0;ResourceLoad* selected=nullptr;
    for(auto* load=diagnosticHead;load;load=load->m_next)if(load->m_state==State::Loading){
        ++active;if(!selected || load->m_post)selected=load;
    }
    if(!selected){std::snprintf(line,sizeof(line),"Network: no pending requests");return line;}
    auto& s=*selected;
    const char* phase=s.m_waitingForSlot?"waiting slot":s.m_deferred?"response policy":!s.m_headersSent?"waiting headers":s.m_info.state==SACX_USER_HTTP_DONE?"draining body":"receiving body";
    std::snprintf(line,sizeof(line),"Network: active=%u %s %s status=%u received=%llu delivered=%llu | %s",active,s.m_post?"POST":"GET",phase,s.m_info.http_status,
        (unsigned long long)s.m_info.received_bytes,(unsigned long long)s.m_received,s.m_diagnosticURL);
    return line;
}
ResourceLoad::~ResourceLoad(){cancel();if(m_previous)m_previous->m_next=m_next;else diagnosticHead=m_next;if(m_next)m_next->m_previous=m_previous;}
}
