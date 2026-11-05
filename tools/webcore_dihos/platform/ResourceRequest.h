#pragma once
#include <WebCore/ResourceRequestBase.h>

namespace WebCore {
using ResourceRequestData = ResourceRequestBase::RequestData;

// Canonical WebCore metadata only. Converting this value is not a network
// operation; the asynchronous loader separately checks transport capabilities.
class ResourceRequest final : public ResourceRequestBase {
public:
    ResourceRequest() : ResourceRequestBase(URL(), ResourceRequestCachePolicy::UseProtocolCachePolicy) { }
    explicit ResourceRequest(String&& url)
        : ResourceRequestBase(URL({ }, WTF::move(url)), ResourceRequestCachePolicy::UseProtocolCachePolicy) { }
    ResourceRequest(URL&& url)
        : ResourceRequestBase(WTF::move(url), ResourceRequestCachePolicy::UseProtocolCachePolicy) { }
    ResourceRequest(URL&& url, const String& referrer,
        ResourceRequestCachePolicy policy = ResourceRequestCachePolicy::UseProtocolCachePolicy)
        : ResourceRequestBase(WTF::move(url), policy) { setHTTPReferrer(referrer); }
    ResourceRequest(ResourceRequestBase&& base) : ResourceRequestBase(WTF::move(base)) { }
    explicit ResourceRequest(ResourceRequestData&& data) : ResourceRequestBase(WTF::move(data)) { }

    bool encodingRequiresPlatformData() const { return false; }
    static ResourceRequest fromResourceRequestData(ResourceRequestData&& data)
    { return ResourceRequest(WTF::move(data)); }
    ResourceRequestData getRequestDataToSerialize() const
    { updateResourceRequest(); return m_requestData; }

private:
    friend class ResourceRequestBase;
    // Only the canonical representation exists. The base invokes these hooks
    // to mark its cached synchronization flags, not to perform transport I/O.
    void doUpdatePlatformRequest() { RELEASE_ASSERT(m_resourceRequestUpdated); }
    void doUpdateResourceRequest() { RELEASE_ASSERT(m_resourceRequestUpdated); }
    void doUpdatePlatformHTTPBody() { RELEASE_ASSERT(m_resourceRequestBodyUpdated); }
    void doUpdateResourceHTTPBody() { RELEASE_ASSERT(m_resourceRequestBodyUpdated); }
};
}

namespace WTF {
template<> struct MarkableTraits<WebCore::ResourceRequest> {
    static bool isEmptyValue(const WebCore::ResourceRequest& request) { return request.isNull(); }
    static WebCore::ResourceRequest emptyValue() { return { }; }
};
}
