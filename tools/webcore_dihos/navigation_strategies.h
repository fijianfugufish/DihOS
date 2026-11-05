// Asynchronous native Page loading. WebCore retains redirect/CORS policy.
#pragma once
#include "LoaderStrategy.h"
#include "PlatformStrategies.h"
#include "ResourceError.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "SubresourceLoader.h"
#include "CachedResource.h"
#include "BlobRegistryDihOS.h"
#if ENABLE(VIDEO)
#include "media/MediaPlayerDihOS.h"
#endif

namespace DihosNavigation {
using namespace WebCore;
using namespace WTF;

class NativeNetworkLoader final : public LoaderStrategy {
    static ResourceError denied(const URL& url) {
        return { "DihOSHeadless"_s, 1, url, "The requested native browser operation is unavailable"_s };
    }
public:
    void loadResource(LocalFrame& frame,CachedResource& resource,ResourceRequest&& request,const ResourceLoaderOptions& options,CompletionHandler<void(RefPtr<SubresourceLoader>&&)>&& completion) final {
        SubresourceLoader::create(frame,resource,WTF::move(request),options,
            [completion=WTF::move(completion)](RefPtr<SubresourceLoader>&& loader) mutable {
                RefPtr retained=loader;completion(WTF::move(loader));
                if(retained && !retained->reachedTerminalState())retained->start();
            });
    }
    void loadResourceSynchronously(FrameLoader&, ResourceLoaderIdentifier, const ResourceRequest& request, ClientCredentialPolicy, const FetchOptions&, const HTTPHeaderMap&, ResourceError& error, ResourceResponse& response, Vector<uint8_t>& data) final { error=denied(request.url()); response={}; data.clear(); }
    // No separate scheduler queue: each ResourceLoader owns and cancels its handle.
    void pageLoadCompleted(Page&) final { }
    void browsingContextRemoved(LocalFrame&) final { }
    void remove(ResourceLoader*) final { }
    void setDefersLoading(ResourceLoader& loader,bool deferred) final {
        // ResourceLoader already deferred its active handle before notifying us.
        // A load paused before start instead owns a deferred request here.
        if(!deferred && !loader.reachedTerminalState() && !loader.deferredRequest().isNull()){
            loader.takeDeferredRequest();loader.start();
        }
    }
    void crossOriginRedirectReceived(ResourceLoader*, const URL&) final { }
    void servePendingRequests(ResourceLoadPriority) final { }
    void suspendPendingRequests() final { }
    void resumePendingRequests() final { }
    void startPingLoad(LocalFrame&, ResourceRequest& request, const HTTPHeaderMap&, const FetchOptions&, ContentSecurityPolicyImposition, PingLoadCompletionHandler&& completion) final { if(completion) completion(denied(request.url()),{}); }
    void preconnectTo(FrameLoader&, ResourceRequest&& request, StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&& completion) final { if(completion) completion(denied(request.url())); }
    void setCaptureExtraNetworkLoadMetricsEnabled(bool) final { }
    bool isOnLine() const final { return true; }
    void addOnlineStateChangeListener(Function<void(bool)>&&) final { } // Link failures are delivered per request.
    void isResourceLoadFinished(CachedResource& resource, CompletionHandler<void(bool)>&& completion) final { completion(!resource.isLoading()); }
    ResourceError cancelledError(const ResourceRequest& r) const final { return {"DihOSNavigation"_s,-125,r.url(),"Load canceled"_s,ResourceError::Type::Cancellation}; }
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

class NavigationStrategies final : public PlatformStrategies {
    NativeNetworkLoader loader;
    LoaderStrategy* createLoaderStrategy() final { return &loader; }
    // Memory blobs use WebCore's native registry; there is no file-backed
    // blob persistence or kernel filesystem access.
    PasteboardStrategy* createPasteboardStrategy() final { sacx_user_exit(180,"Page: unexpected clipboard strategy"); }
    MediaStrategy* createMediaStrategy() final {
#if ENABLE(VIDEO)
        return createDihOSMediaStrategy();
#else
        sacx_user_exit(180,"Page: unexpected media strategy");
#endif
    }
    BlobRegistry* createBlobRegistry() final { return DihOSWeb::createMemoryBlobRegistry(); }
#if ENABLE(DECLARATIVE_WEB_PUSH)
    PushStrategy* createPushStrategy() final { sacx_user_exit(180,"Page: unexpected push strategy"); }
#endif
};
} // namespace DihosNavigation
