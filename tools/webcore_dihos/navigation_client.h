#pragma once
#include "EmptyFrameLoaderClient.h"
#include "DocumentLoader.h"
#include "FrameLoader.h"
#include "HistoryController.h"
#include "HistoryItem.h"
#include "FrameNetworkingContext.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "ResourceError.h"
#include <wtf/text/CString.h>
#include <sacx_user_api.h>
#include "browser_repaint.h"
#include "HTMLFrameOwnerElement.h"
#include "FrameTreeSyncData.h"
#include "FrameIdentifier.h"
#include "DocumentPage.h"
#include "NodeDocument.h"
#include "DocumentView.h"

namespace DihosNavigation {
using namespace WebCore;
using namespace WTF;
struct NavigationStatus {
    unsigned completed { },failed { };
    int error { };
    bool dirty { true };
    bool errorPagePending { };
    String errorDescription;
    URL errorURL;
    BrowserRepaint repaint;
    unsigned liveSubframes{},finishedSubframes{},failedSubframes{};
};
class NativeContext final : public FrameNetworkingContext {
public:
    explicit NativeContext(LocalFrame& frame):FrameNetworkingContext(&frame){}
    // Page's session jar owns cookies; there is no platform credential store.
    NetworkStorageSession* storageSession() const final { return nullptr; }
};
class NavigationClient final : public EmptyFrameLoaderClient {
public:
    NavigationClient(LocalFrame& frame,FrameLoader& loader,NavigationStatus& status)
        :EmptyFrameLoaderClient(loader),m_frame(frame),m_status(status),m_subframe(!frame.isMainFrame()){
        if(m_subframe)++m_status.liveSubframes;
    }
    ~NavigationClient(){if(m_subframe)--m_status.liveSubframes;}
private:
    RefPtr<LocalFrame> createFrame(const AtomString& name,HTMLFrameOwnerElement& owner) final {
        if(m_status.liveSubframes>=32 || !m_frame->page())return nullptr;
        unsigned depth=0;for(auto* ancestor=&m_frame.get();ancestor;ancestor=dynamicDowncast<LocalFrame>(ancestor->tree().parent()))
            if(++depth>=16)return nullptr;
        // Inherit restrictions. Never substitute an unrestricted about:blank
        // context for an iframe's sandbox or referrer policy.
        auto sandbox=owner.sandboxFlags();sandbox.add(m_frame->effectiveSandboxFlags());
        auto referrer=owner.referrerPolicy();
        if(referrer==ReferrerPolicy::EmptyString)referrer=owner.document().referrerPolicy();
        auto frame=LocalFrame::createSubframe(*m_frame->page(),[status=&m_status](LocalFrame& child,FrameLoader& loader){
            return makeUniqueRefWithoutRefCountedCheck<NavigationClient>(child,loader,*status);
        },generateFrameIdentifier(),sandbox,referrer,owner,FrameTreeSyncData::create());
        frame->tree().setSpecifiedName(name);
        frame->createView(IntSize(300,150),Color::white,IntSize(),false,ScrollbarMode::Auto,false,ScrollbarMode::Auto,false);
        frame->init();
        return frame->page()?RefPtr<LocalFrame>{frame}:nullptr;
    }
    static bool allowed(const ResourceRequest& request) {
        const auto& url=request.url();
        return url.isEmpty() || url.string()=="about:blank"_s || url.string()=="about:srcdoc"_s ||
            (url.isValid() && url.protocolIsInHTTPFamily() && url.user().isEmpty() && url.password().isEmpty());
    }
    bool hasWebView() const final { return true; }
    RefPtr<HistoryItem> createHistoryItemTree(bool clip,BackForwardItemIdentifier id) const final {
        return m_frame->loader().history().createItemTree(m_frame.get(),clip,id);
    }
    ShouldGoToHistoryItem shouldGoToHistoryItem(HistoryItem& item,IsSameDocumentNavigation) const final {
        return allowed(ResourceRequest(URL{{},item.urlString()}))?ShouldGoToHistoryItem::Yes:ShouldGoToHistoryItem::No;
    }
    bool isEmptyFrameLoaderClient() const final { return false; }
    bool shouldUseCredentialStorage(DocumentLoader*,ResourceLoaderIdentifier) final { return false; }
    Ref<FrameNetworkingContext> createNetworkingContext() final { return adoptRef(*new NativeContext(m_frame.get())); }
    void committedLoad(DocumentLoader* loader,const SharedBuffer& data) final { loader->commitData(data);m_status.dirty=true; }
    bool canHandleRequest(const ResourceRequest& request) const final { return allowed(request); }
    bool canShowMIMEType(const String& type) const final { return type=="text/html"_s || type=="application/xhtml+xml"_s || type=="text/plain"_s; }
    bool canShowMIMETypeAsHTML(const String& type) const final { return type=="text/html"_s; }
    String userAgent(const URL&) const final { return "DihOS/0.3 Dihscover WebKit"_s; }
    void dispatchDecidePolicyForResponse(const ResourceResponse& response,const ResourceRequest&,const String&,FramePolicyFunction&& decision) final {
        decision(canShowMIMEType(response.mimeType())?PolicyAction::Use:PolicyAction::Ignore);
    }
    void dispatchDecidePolicyForNewWindowAction(const NavigationAction&,const ResourceRequest&,FormState*,const String&,std::optional<HitTestResult>&&,FramePolicyFunction&& decision) final { decision(PolicyAction::Ignore); }
    void dispatchDecidePolicyForNavigationAction(const NavigationAction&,const ResourceRequest& request,const ResourceResponse&,FormState*,const String&,std::optional<NavigationIdentifier>,std::optional<HitTestResult>&&,bool,NavigationUpgradeToHTTPSBehavior,SandboxFlags,PolicyDecisionMode,FramePolicyFunction&& decision) final {
        decision(allowed(request)?PolicyAction::Use:PolicyAction::Ignore);
    }
    void dispatchDidFailProvisionalLoad(const ResourceError& error,WillContinueLoading,WillInternallyHandleFailure) final { failure(error); }
    void dispatchDidFailLoad(const ResourceError& error) final { failure(error); }
    void dispatchDidFinishLoad() final {
        if(m_subframe){++m_status.finishedSubframes;m_status.dirty=true;return;}
        if(m_frame->document()->url().protocolIsInHTTPFamily())m_status.error=0;
        ++m_status.completed;m_status.dirty=true;sacx_user_log("Native navigation: document finished");
    }
    void transitionToCommittedForNewPage(InitializingIframe) final {
        if(m_subframe && !m_frame->view())m_frame->createView(IntSize(300,150),Color::white,IntSize(),false,ScrollbarMode::Auto,false,ScrollbarMode::Auto,false);
        m_status.dirty=true;
    }
    void failure(const ResourceError& error){
        if(m_subframe){if(!error.isCancellation())++m_status.failedSubframes;m_status.dirty=true;return;}
        if(error.isCancellation()){m_status.dirty=true;return;}
        ++m_status.failed;m_status.error=error.errorCode();m_status.dirty=true;
        m_status.errorDescription=error.localizedDescription();m_status.errorURL=error.failingURL();m_status.errorPagePending=true;
        sacx_user_log("Native navigation: load failed");sacx_user_log(error.localizedDescription().utf8().data());
    }
    WeakRef<LocalFrame> m_frame;
    NavigationStatus& m_status;
    bool m_subframe;
};
}
