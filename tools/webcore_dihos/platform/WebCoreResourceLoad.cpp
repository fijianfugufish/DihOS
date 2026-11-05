#include "config.h"
#include "WebCoreResourceLoad.h"
#include "HTTPHeaderNames.h"
#include "HTTPParsers.h"
#include "FormData.h"
#include "SessionCookiesDihOS.h"
#include "ResponseHeadersDihOS.h"
#include "sdk/sacx_user_http_request.h"
#include "sdk/http_request_headers.h"
#include <wtf/text/CString.h>

namespace DihOSWeb {
using namespace WTF;
using namespace WebCore;
Ref<WebCoreResourceLoad> WebCoreResourceLoad::create(RunLoop& loop,DispatchQueue& queue)
{ return adoptRef(*new WebCoreResourceLoad(ResourceLoad::createManual(loop,queue))); }
Ref<WebCoreResourceLoad> WebCoreResourceLoad::create(RunLoop& loop,DispatchQueue& queue,ResourceTransport transport)
{ return adoptRef(*new WebCoreResourceLoad(ResourceLoad::create(loop,queue,transport))); }
WebCoreResourceLoad::WebCoreResourceLoad(Ref<ResourceLoad>&& load) : m_load(WTF::move(load)) { }

int WebCoreResourceLoad::start(const ResourceRequest& request,Callbacks&& callbacks,unsigned timeoutMs)
{
    if(m_started || m_cancelled)return -16;
    if(!callbacks.complete)return -22;
    bool post=request.httpMethod()=="POST"_s;
    if(!post && request.httpMethod()!="GET"_s)return -95;
    Vector<uint8_t> body;
    if(auto* form=request.httpBody()){
        for(const auto& element:form->elements()){
            auto* data=std::get_if<Vector<uint8_t>>(&element.data);
            if(!data)return -95; // Never flatten away a file or blob upload.
            if(data->size()>SACX_USER_HTTP_BODY_MAX-body.size())return -75;
            body.append(data->span());
        }
    }
    if(!post && !body.isEmpty())return -95;
    if(!request.url().isValid() || !request.url().protocolIsInHTTPFamily() ||
       !request.url().user().isEmpty() || !request.url().password().isEmpty())return -22;
    m_url=request.url();m_url.removeFragmentIdentifier();auto url=m_url.string().utf8();
    m_allowCookies=request.allowCookies();m_cookieSite=SameSiteInfo::create(request);
    Vector<char> fields;
    for(const auto& field:request.httpHeaderFields()){
        // Recompute for every URL/credential mode, including redirects. Never
        // retain a Cookie header belonging to a previous redirect origin.
        if(equalLettersIgnoringASCIICase(field.key,"cookie"_s))continue;
        // The broker derives framing from its owned byte copy.
        if(post && equalLettersIgnoringASCIICase(field.key,"content-length"_s))continue;
        auto name=field.key.utf8(),value=field.value.utf8();
        if(name.length()+value.length()+4>SACX_USER_HTTP_REQUEST_HEADERS_MAX-fields.size())return -75;
        fields.append(name.span());fields.append(':');fields.append(' ');
        fields.append(value.span());fields.append('\r');fields.append('\n');
    }
    auto cookies=requestCookies(request,SACX_USER_HTTP_REQUEST_HEADERS_MAX-fields.size()>10?SACX_USER_HTTP_REQUEST_HEADERS_MAX-fields.size()-10:0);
    if(!cookies)return -75;
    if(!cookies->isEmpty()){
        auto value=cookies->utf8();fields.append(std::span{"Cookie: ",8});fields.append(value.span());fields.append('\r');fields.append('\n');
    }
    int valid=dihos_http_validate_request_headers(fields.span().data(),fields.size());if(valid)return valid;
    m_callbacks=WTF::move(callbacks);m_started=true;
    ResourceLoad::Callbacks delivery={
        [weakThis=WeakPtr{*this}](const auto& info){if(weakThis)weakThis->headers(info);},
        [weakThis=WeakPtr{*this}](const uint8_t* bytes,unsigned count){if(weakThis)weakThis->data(bytes,count);},
        [weakThis=WeakPtr{*this}](int error){if(weakThis)weakThis->finish(error);}};
    int result=post?m_load->startPost(url.data(),url.length(),fields.span().data(),fields.size(),body.span().data(),body.size(),WTF::move(delivery),timeoutMs):
        m_load->startWithHeaders(url.data(),url.length(),fields.span().data(),fields.size(),WTF::move(delivery),timeoutMs);
    if(result){m_finished=true;m_callbacks={};}
    return result;
}

void WebCoreResourceLoad::headers(const sacx_user_http_info& info)
{
    Ref protectedThis{*this};
    if(m_cancelled || m_finished)return;
    Vector<uint8_t> raw;raw.grow(SACX_USER_HTTP_HEADERS_MAX);
    unsigned length=0;
    while(length<raw.size()){
        unsigned capacity=std::min<unsigned>(SACX_USER_HTTP_READ_MAX,raw.size()-length);
        int count=m_load->copyHeaders(raw.mutableSpan().data()+length,capacity,length);
        if(count<0){finish(count);return;}
        if(!count)break;
        length+=count;
    }
    if(length==raw.size()){
        uint8_t extra;int result=m_load->copyHeaders(&extra,1,length);
        if(result){finish(result<0?result:-75);return;}
    }
    raw.shrink(length);
    unsigned line=0;
    while(line+1<length && !(raw[line]=='\r' && raw[line+1]=='\n'))++line;
    if(line<12 || line+1>=length || raw[0]!='H' || raw[1]!='T' || raw[2]!='T' ||
       raw[3]!='P' || raw[4]!='/' || raw[5]!='1' || raw[6]!='.' ||
       (raw[7]!='0' && raw[7]!='1') || raw[8]!=' ' || info.http_status<200 ||
       raw[9]!='0'+info.http_status/100 || raw[10]!='0'+info.http_status/10%10 ||
       raw[11]!='0'+info.http_status%10){finish(-71,"Invalid HTTP response status line");return;}
    ResourceResponse response(URL(m_url),String(),-1,String());
    response.setHTTPStatusCode(info.http_status);
    response.setHTTPStatusText(String(extractReasonPhraseFromHTTPStatusLine(String(raw.span().first(line))).string()));
    response.setHTTPVersion(String(raw.span().first(8)));
    unsigned offset=line+2;Vector<String> cookieFields;
    for(;;){
        DihosResponseField field{};int parsed=dihos_response_field(raw.span().data(),length,offset,&field);
        if(parsed==1)break;
        if(parsed<0){finish(-71,"Invalid HTTP response header syntax");return;}
        auto name=String(raw.span().subspan(field.name,field.nameBytes));
        auto value=field.valueBytes?String(raw.span().subspan(field.value,field.valueBytes)):emptyString();
        if(!isValidHTTPHeaderValue(value)){finish(-71,"Invalid HTTP response header value");return;}
        if(m_allowCookies&&equalLettersIgnoringASCIICase(name,"set-cookie"_s))cookieFields.append(value);
        response.addHTTPHeaderField(name,value);offset=field.next;
    }
    auto contentType=response.httpHeaderField(HTTPHeaderName::ContentType);
    response.setMimeType(extractMIMETypeFromMediaType(contentType));
    response.setTextEncodingName(extractCharsetFromMediaType(contentType).toString());
    // Only after the complete header block was validated. ResourceLoad already
    // rejected unauthenticated TLS before delivering the header callback.
    for(const auto& cookie:cookieFields)responseCookie(m_url,m_cookieSite,cookie);
    // CertificateInfo remains unavailable; ResourceLoad has already rejected
    // unverified TLS. Redirect responses remain visible to the WebCore client.
    m_responseSent=true;
    auto callback=WTF::move(m_callbacks.response);
    if(callback)callback(WTF::move(response));
}
void WebCoreResourceLoad::data(const uint8_t* bytes,unsigned count)
{
    Ref protectedThis{*this};
    if(m_cancelled || m_finished)return;
    if(!m_responseSent){finish(-71);return;}
    auto callback=WTF::move(m_callbacks.data);
    if(callback)callback(std::span{bytes,count});
    if(!m_cancelled && !m_finished)m_callbacks.data=WTF::move(callback);
}
void WebCoreResourceLoad::finish(int error,const char* detail)
{
    Ref protectedThis{*this};
    if(m_cancelled || m_finished)return;
    if(!error && !m_responseSent)error=-71;
    m_finished=true;m_load->cancel();
    auto callback=WTF::move(m_callbacks.complete);m_callbacks={};
    if(callback){
        if(error){
            const auto& info=m_load->metadata();unsigned length=0;
            while(length<sizeof(info.error) && info.error[length])++length;
            auto message=detail?String::fromLatin1(detail):length?String::fromUTF8(std::span{info.error,length}):
                error==-13?"HTTPS response authentication failed"_s:
                error==-75?"Response exceeds the 16 MiB browser limit"_s:
                error==-110?"Request timed out while waiting for the server or a network slot"_s:
                error==-24?"Too many pending resources; try loading this page again"_s:
                error==-12?"Not enough memory for the network response"_s:"Native resource request failed"_s;
            callback(ResourceError("DihOSNetwork"_s,error,m_url,WTF::move(message),
                error==-110?ResourceError::Type::Timeout:ResourceError::Type::General));
        }
        else callback(ResourceError());
    }
}
void WebCoreResourceLoad::cancel(){m_cancelled=true;m_load->cancel();m_callbacks={};}
WebCoreResourceLoad::~WebCoreResourceLoad(){cancel();}
}
