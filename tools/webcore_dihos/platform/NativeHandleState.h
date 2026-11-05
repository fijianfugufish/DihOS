#pragma once
#include "WebCoreResourceLoad.h"
namespace WebCore { class ResourceHandle; }
namespace DihOSWeb {
// Only the owner RunLoop accesses the handle. Weak callbacks and an explicit
// detach avoid retaining a canceled Frame/ResourceHandle through transport IO.
class NativeHandleState final : public WTF::RefCounted<NativeHandleState>,public WTF::CanMakeWeakPtr<NativeHandleState> {
public:
    NativeHandleState(WebCore::ResourceHandle&,bool);
    bool start(WebCore::ResourceRequest&&);
    void cancel();
    void setDeferred(bool);
    ~NativeHandleState();
private:
    void begin();
    void response(WebCore::ResourceResponse&&);
    void fail(WebCore::ResourceError&&);
    WebCore::ResourceHandle* m_owner;
    WTF::Ref<DispatchQueue> m_queue;
    WTF::RefPtr<WebCoreResourceLoad> m_load;
    WebCore::ResourceRequest m_request;
    uint64_t m_post { },m_generation { };
    unsigned m_redirects { };
    bool m_deferred { },m_policyPending { };
};
}
