#include "config.h"
#include "NativeHandleState.h"
#include "ResourceHandle.h"
#include "SecurityOrigin.h"
#include "ResourceHandleInternal.h"
#include "ResourceHandleClient.h"
#include "NetworkLoadMetrics.h"
#include "SharedBuffer.h"
#include "HTTPHeaderNames.h"
#include "browser_diagnostics.h"

namespace DihOSWeb {
using namespace WebCore;
using namespace WTF;
NativeHandleState::NativeHandleState(ResourceHandle& owner,bool deferred)
    :m_owner(&owner),m_queue(DispatchQueue::create(RunLoop::mainSingleton())),m_deferred(deferred){}
bool NativeHandleState::start(ResourceRequest&& request)
{
    if(!m_owner)return false;
    m_request=WTF::move(request);
    auto posted=m_queue->post([weakThis=WeakPtr{*this}]{if(weakThis){weakThis->m_post=0;weakThis->begin();}});
    m_post=posted.id;return posted.error==DispatchQueue::Error::None;
}
void NativeHandleState::begin()
{
    Ref protectedThis{*this};
    if(!m_owner)return;
    m_load=WebCoreResourceLoad::create(RunLoop::mainSingleton(),m_queue);
    m_load->setDeferred(m_deferred);
    auto generation=++m_generation;
    int result=m_load->start(m_request,{
        [weakThis=WeakPtr{*this},generation](ResourceResponse&& response){
            if(weakThis && generation==weakThis->m_generation)weakThis->response(WTF::move(response));},
        [weakThis=WeakPtr{*this},generation](std::span<const uint8_t> bytes){
            if(!weakThis || generation!=weakThis->m_generation || !weakThis->m_owner)return;
            Ref state{*weakThis};Ref owner{*state->m_owner};
            if(auto* client=owner->client()){auto data=SharedBuffer::create(bytes);client->didReceiveData(owner.ptr(),data.get(),bytes.size());}},
        [weakThis=WeakPtr{*this},generation](ResourceError&& error){
            if(!weakThis || generation!=weakThis->m_generation || !weakThis->m_owner)return;
            Ref state{*weakThis};Ref owner{*state->m_owner};
            if(!error.isNull()){state->fail(WTF::move(error));return;}
            if(auto* client=owner->client()){NetworkLoadMetrics metrics;client->didFinishLoading(owner.ptr(),metrics);}
        }});
    if(result)fail({"DihOSNetwork"_s,result,m_request.url(),
        result==-36?"URL exceeds the 8191-byte native request limit"_s:
        result==-95?"The requested method, upload source or header is unavailable (native GET and memory-only POST are supported)"_s:
        result==-75?"Request metadata or form data exceed the 16 KiB per-field-group native limits"_s:
        result==-12?"Not enough memory for the network response"_s:
        result==-24?"Too many pending resources; try loading this page again"_s:"Request could not be queued"_s});
}
void NativeHandleState::response(ResourceResponse&& response)
{
    Ref protectedThis{*this};
    if(!m_owner)return;
    Ref owner{*m_owner};auto* client=owner->client();if(!client){cancel();return;}
    if(response.isRedirection() && !response.httpHeaderField(HTTPHeaderName::Location).isEmpty()){
        if(++m_redirects>8){fail({"DihOSNetwork"_s,-40,m_request.url(),"Redirect limit exceeded"_s});return;}
        auto request=m_request.redirectedRequest(response,true);
        // Frame/ResourceLoader owns redirect security and can replace or reject
        // the request. Cancel the old response before invoking that policy.
        m_load->cancel();++m_generation;
        client->willSendRequestAsync(owner.ptr(),WTF::move(request),WTF::move(response),
            [weakThis=WeakPtr{*this}](ResourceRequest&& next){
                if(!weakThis || !weakThis->m_owner)return;
                if(next.isNull()){weakThis->cancel();return;}
                if(!weakThis->start(WTF::move(next)))weakThis->fail({"DihOSNetwork"_s,-125,weakThis->m_request.url(),"Redirect canceled"_s});
            });
        return;
    }
    m_policyPending=true;m_load->setDeferred(true);auto generation=m_generation;
    owner->didReceiveResponse(WTF::move(response),[weakThis=WeakPtr{*this},generation]{
        if(!weakThis || !weakThis->m_owner || generation!=weakThis->m_generation)return;
        weakThis->m_policyPending=false;weakThis->m_load->setDeferred(weakThis->m_deferred);
    });
}
void NativeHandleState::fail(ResourceError&& error)
{
    if(!error.isCancellation()){
        auto url=error.failingURL().string().utf8(),reason=error.localizedDescription().utf8();
        DihosNavigation::browserDiagnostics.failed(std::string_view(url.data(),url.length()),error.errorCode(),std::string_view(reason.data(),reason.length()));
    }
    Ref protectedThis{*this};if(!m_owner)return;Ref owner{*m_owner};
    if(m_load)m_load->cancel();
    if(auto* client=owner->client())client->didFail(owner.ptr(),error);
}
void NativeHandleState::setDeferred(bool deferred)
{m_deferred=deferred;if(m_load)m_load->setDeferred(deferred || m_policyPending);}
void NativeHandleState::cancel()
{
    m_owner=nullptr;++m_generation;
    if(m_post){m_queue->cancel(m_post);m_post=0;}
    if(m_load)m_load->cancel();m_load=nullptr;m_queue->close();
}
NativeHandleState::~NativeHandleState(){cancel();}
}
namespace WebCore {
using namespace WTF;
ResourceHandleInternal::~ResourceHandleInternal()=default;
ResourceHandle::~ResourceHandle(){cancel();}
bool ResourceHandle::start()
{
    d->m_nativeState=adoptRef(*new DihOSWeb::NativeHandleState(*this,d->m_defersLoading));
    return d->m_nativeState->start(ResourceRequest(firstRequest()));
}
void ResourceHandle::cancel(){if(d->m_nativeState)d->m_nativeState->cancel();d->m_nativeState=nullptr;}
void ResourceHandle::platformSetDefersLoading(bool deferred){if(d->m_nativeState)d->m_nativeState->setDeferred(deferred);}
void ResourceHandle::platformLoadResourceSynchronously(NetworkingContext*,const ResourceRequest& request,StoredCredentialsPolicy,SecurityOrigin*,ResourceError& error,ResourceResponse& response,Vector<uint8_t>& data)
{error={"DihOSNetwork"_s,-95,request.url(),"Synchronous networking is unavailable"_s};response={};data.clear();}
bool ResourceHandle::shouldUseCredentialStorage(){return false;}
void ResourceHandle::didReceiveAuthenticationChallenge(const AuthenticationChallenge&){cancel();}
void ResourceHandle::receivedCredential(const AuthenticationChallenge&,const Credential&){cancel();}
void ResourceHandle::receivedRequestToContinueWithoutCredential(const AuthenticationChallenge&){cancel();}
void ResourceHandle::receivedCancellation(const AuthenticationChallenge&){cancel();}
void ResourceHandle::receivedRequestToPerformDefaultHandling(const AuthenticationChallenge&){cancel();}
void ResourceHandle::receivedChallengeRejection(const AuthenticationChallenge&){cancel();}
}
