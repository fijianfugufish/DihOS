#pragma once
#include "ResourceLoad.h"
#include <WebCore/ResourceError.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/ResourceResponse.h>
#include <WebCore/SameSiteInfo.h>

namespace DihOSWeb {
// Owner-thread boundary between checked SACX streaming and WebCore values.
// Frame/ResourceHandle clients still own redirect, CORS and navigation policy.
class WebCoreResourceLoad final : public WTF::RefCounted<WebCoreResourceLoad>, public WTF::CanMakeWeakPtr<WebCoreResourceLoad> {
public:
    struct Callbacks {
        WTF::Function<void(WebCore::ResourceResponse&&)> response;
        WTF::Function<void(std::span<const uint8_t>)> data;
        WTF::Function<void(WebCore::ResourceError&&)> complete;
    };
    static WTF::Ref<WebCoreResourceLoad> create(WTF::RunLoop&,DispatchQueue&);
    // Deterministic test seam; start must have manual-redirect semantics.
    static WTF::Ref<WebCoreResourceLoad> create(WTF::RunLoop&,DispatchQueue&,ResourceTransport);
    int start(const WebCore::ResourceRequest&,Callbacks&&,unsigned timeoutMs=30000);
    void cancel();
    void setDeferred(bool deferred) { m_load->setDeferred(deferred); }
    ~WebCoreResourceLoad();
private:
    explicit WebCoreResourceLoad(WTF::Ref<ResourceLoad>&&);
    void headers(const sacx_user_http_info&);
    void data(const uint8_t*,unsigned);
    void finish(int,const char* detail=nullptr);
    WTF::Ref<ResourceLoad> m_load;
    WTF::URL m_url;
    WebCore::SameSiteInfo m_cookieSite;
    bool m_allowCookies { false };
    Callbacks m_callbacks;
    bool m_started { false },m_cancelled { false },m_responseSent { false },m_finished { false };
};
}
