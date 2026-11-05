// Local-HTML fixture policy only. Not the browser's network implementation.
#pragma once
#include "LoaderStrategy.h"
#include "PlatformStrategies.h"
#include "ResourceError.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "SubresourceLoader.h"

namespace DihosHeadless {
using namespace WebCore;
using namespace WTF;

class LocalOnlyLoader final : public LoaderStrategy {
    static ResourceError denied(const URL& url) {
        return { "DihOSHeadless"_s, 1, url, "External loading is disabled in the local Page fixture"_s };
    }
public:
    void loadResource(LocalFrame&, CachedResource&, ResourceRequest&&, const ResourceLoaderOptions&, CompletionHandler<void(RefPtr<SubresourceLoader>&&)>&& completion) final { completion(nullptr); }
    void loadResourceSynchronously(FrameLoader&, ResourceLoaderIdentifier, const ResourceRequest& request, ClientCredentialPolicy, const FetchOptions&, const HTTPHeaderMap&, ResourceError& error, ResourceResponse& response, Vector<uint8_t>& data) final { error=denied(request.url()); response={}; data.clear(); }
    // No requests are admitted, so these scheduler/lifecycle notifications
    // have no queued transport work to mutate.
    void pageLoadCompleted(Page&) final { }
    void browsingContextRemoved(LocalFrame&) final { }
    void remove(ResourceLoader*) final { }
    void setDefersLoading(ResourceLoader&, bool) final { }
    void crossOriginRedirectReceived(ResourceLoader*, const URL&) final { }
    void servePendingRequests(ResourceLoadPriority) final { }
    void suspendPendingRequests() final { }
    void resumePendingRequests() final { }
    void startPingLoad(LocalFrame&, ResourceRequest& request, const HTTPHeaderMap&, const FetchOptions&, ContentSecurityPolicyImposition, PingLoadCompletionHandler&& completion) final { if(completion) completion(denied(request.url()),{}); }
    void preconnectTo(FrameLoader&, ResourceRequest&& request, StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&& completion) final { if(completion) completion(denied(request.url())); }
    void setCaptureExtraNetworkLoadMetricsEnabled(bool) final { }
    bool isOnLine() const final { return false; }
    void addOnlineStateChangeListener(Function<void(bool)>&&) final { } // Permanently offline.
    void isResourceLoadFinished(CachedResource&, CompletionHandler<void(bool)>&& completion) final { completion(true); }
    ResourceError cancelledError(const ResourceRequest& r) const final { return denied(r.url()); }
    ResourceError blockedError(const ResourceRequest& r) const final { return denied(r.url()); }
    bool isBlockedError(const ResourceError& e) const final { return e.domain()=="DihOSHeadless"_s && e.errorCode()==1; }
    ResourceError blockedByContentBlockerError(const ResourceRequest& r) const final { return denied(r.url()); }
    ResourceError cannotShowURLError(const ResourceRequest& r) const final { return denied(r.url()); }
    ResourceError interruptedForPolicyChangeError(const ResourceRequest& r) const final { return denied(r.url()); }
#if ENABLE(CONTENT_FILTERING)
    ResourceError blockedByContentFilterError(const ResourceRequest& r) const final { return denied(r.url()); }
#endif
    ResourceError cannotShowMIMETypeError(const ResourceResponse& r) const final { return denied(r.url()); }
    ResourceError fileDoesNotExistError(const ResourceResponse& r) const final { return denied(r.url()); }
    ResourceError httpsUpgradeRedirectLoopError(const ResourceRequest& r) const final { return denied(r.url()); }
    ResourceError httpNavigationWithHTTPSOnlyError(const ResourceRequest& r) const final { return denied(r.url()); }
    bool isHttpNavigationWithHTTPSOnlyError(const ResourceError&) const final { return false; }
    ResourceError pluginWillHandleLoadError(const ResourceResponse& r) const final { return denied(r.url()); }
};

class LocalPageStrategies final : public PlatformStrategies {
    LocalOnlyLoader loader;
    LoaderStrategy* createLoaderStrategy() final { return &loader; }
    // This fixture has no clipboard, media or blob operations. Fail the test
    // explicitly if Page initialization unexpectedly requires one of them.
    PasteboardStrategy* createPasteboardStrategy() final { sacx_user_exit(180,"Page: unexpected clipboard strategy"); }
    MediaStrategy* createMediaStrategy() final { sacx_user_exit(180,"Page: unexpected media strategy"); }
    BlobRegistry* createBlobRegistry() final { sacx_user_exit(180,"Page: unexpected blob strategy"); }
#if ENABLE(DECLARATIVE_WEB_PUSH)
    PushStrategy* createPushStrategy() final { sacx_user_exit(180,"Page: unexpected push strategy"); }
#endif
};
} // namespace DihosHeadless
