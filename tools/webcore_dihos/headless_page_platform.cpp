// Platform contracts shared by native Page diagnostics and shipping Dihscover.
// Rendering uses owned Cairo surfaces. Unsupported facilities remain explicit
// errors; software view transitions use WebCore's skip lifecycle instead.
#include "config.h"
#include <sacx_user_api.h>
#include "AXObjectCache.h"
#include "AccessibilityObject.h"
#include "DestinationColorSpace.h"
#include "Editor.h"
#include "FloatRect.h"
#include "GraphicsLayer.h"
#include "MIMETypeRegistry.h"
#include "NetworkStorageSession.h"
#include "Pasteboard.h"
#include "PlatformKeyboardEvent.h"
#include "PlatformScreen.h"
#include "PublicSuffixStore.h"
#include "RenderTheme.h"
#include "ResourceHandle.h"
#include "ScrollbarThemeMock.h"
#include "SystemFontDatabase.h"
#include "Theme.h"
#include <wtf/NeverDestroyed.h>
#include <wtf/text/WTFString.h>
#include <new>
#include <cstdlib>
#ifdef DIHOS_PAGE_NAVIGATION
#include "SessionCookiesDihOS.h"
#endif

[[noreturn]] static void unavailable(const char* operation)
{
    sacx_user_exit(180,operation);
}

// Required by libc++'s allocation-failure-aware containers. Allocation is
// still through the process-owned SACX heap and returns null on exhaustion.
namespace std { const nothrow_t nothrow {}; }
void* operator new(size_t n,const std::nothrow_t&) noexcept {return malloc(n?n:1);}
void* operator new[](size_t n,const std::nothrow_t& t) noexcept {return ::operator new(n,t);}
extern "C" size_t strspn(const char* s,const char* accept)
{
    size_t n=0;for(;s[n];++n){const char* p=accept;while(*p && *p!=s[n])++p;if(!*p)break;}return n;
}
extern "C" size_t strcspn(const char* s,const char* reject)
{
    size_t n=0;for(;s[n];++n){const char* p=reject;while(*p && *p!=s[n])++p;if(*p)break;}return n;
}

namespace PAL { void systemBeep(){unavailable("Page: unexpected sound");} }
namespace WebCore {
using namespace WTF;
class LocalRenderTheme final : public RenderTheme { };
RenderTheme& RenderTheme::singleton(){static NeverDestroyed<LocalRenderTheme> theme;return theme;}
class LocalTheme final : public Theme { };
Theme& Theme::singleton(){static NeverDestroyed<LocalTheme> theme;return theme;}
ScrollbarTheme& ScrollbarTheme::nativeTheme(){static NeverDestroyed<ScrollbarThemeMock> theme;return theme;}
SystemFontDatabase& SystemFontDatabase::singleton(){static NeverDestroyed<SystemFontDatabase> database=SystemFontDatabase();return database;}
auto SystemFontDatabase::platformSystemFontShorthandInfo(FontShorthand) -> SystemFontShorthandInfo
{return {AtomString("Noto Sans"_s),16,normalWeightValue()};}
void SystemFontDatabase::platformInvalidate(){} // Fixed, immutable memory font set.

// Describe this fixture's actual owned offscreen surface, never host displays.
int screenDepth(Widget*){return 24;}
int screenDepthPerComponent(Widget*){return 8;}
bool screenIsMonochrome(Widget*){return false;}
bool screenHasInvertedColors(){return false;}
FloatRect screenRect(Widget*){return {0,0,640,480};}
FloatRect screenAvailableRect(Widget* w){return screenRect(w);}
bool screenSupportsExtendedColor(Widget*){return false;}
DestinationColorSpace screenColorSpace(Widget*){return DestinationColorSpace::SRGB();}
OptionSet<PlatformEvent::Modifier> PlatformKeyboardEvent::currentStateOfModifierKeys(){return {};}
Ref<GraphicsLayer> GraphicsLayer::create(GraphicsLayerFactory*,GraphicsLayerClient&,Type)
{unavailable("Page: unexpected accelerated graphics layer");}
bool MIMETypeRegistry::isApplicationPluginMIMEType(const String&){return false;} // No plugins.
String MIMETypeRegistry::mimeTypeForExtension(StringView extension){
    // This is type identification, not filesystem access or a promise that
    // a codec is available. Unknown extensions fall back to response sniffing.
    struct Type { ASCIILiteral extension;ASCIILiteral mime; };
    static constexpr Type types[]={
        {"mp4"_s,"video/mp4"_s},{"m4v"_s,"video/mp4"_s},{"webm"_s,"video/webm"_s},
        {"html"_s,"text/html"_s},{"htm"_s,"text/html"_s},{"css"_s,"text/css"_s},
        {"js"_s,"text/javascript"_s},{"mjs"_s,"text/javascript"_s},{"json"_s,"application/json"_s},
        {"png"_s,"image/png"_s},{"jpg"_s,"image/jpeg"_s},{"jpeg"_s,"image/jpeg"_s},
        {"gif"_s,"image/gif"_s},{"svg"_s,"image/svg+xml"_s},{"webp"_s,"image/webp"_s},
        {"bmp"_s,"image/bmp"_s},{"txt"_s,"text/plain"_s},{"vtt"_s,"text/vtt"_s},
        {"woff"_s,"font/woff"_s},{"woff2"_s,"font/woff2"_s},{"ttf"_s,"font/ttf"_s}
    };
    for(auto& type:types)if(equalLettersIgnoringASCIICase(extension,type.extension))return type.mime;
    return { };
}
#ifdef DIHOS_PAGE_NAVIGATION
// Use the pinned official PSL, including private hosting and exception rules.
// Never infer registrable domains merely by chopping labels.
bool PublicSuffixStore::platformIsPublicSuffix(StringView host) const {return DihOSWeb::cookiePublicSuffix(host);}
String PublicSuffixStore::platformTopPrivatelyControlledDomain(StringView host) const {return DihOSWeb::cookieRegistrableDomain(host);}
#else
bool PublicSuffixStore::platformIsPublicSuffix(StringView) const {unavailable("Page: unexpected public suffix lookup");}
String PublicSuffixStore::platformTopPrivatelyControlledDomain(StringView) const {unavailable("Page: unexpected domain lookup");}
#endif

Pasteboard::Pasteboard(std::unique_ptr<PasteboardContext>&& c):m_context(WTF::move(c)){}
std::unique_ptr<Pasteboard> Pasteboard::createForCopyAndPaste(std::unique_ptr<PasteboardContext>&&)
{unavailable("Page: unexpected clipboard creation");}
#define NO_CLIPBOARD unavailable("Page: unexpected clipboard access")
bool Pasteboard::hasData(){NO_CLIPBOARD;}
Vector<String> Pasteboard::typesSafeForBindings(const String&){NO_CLIPBOARD;}
Vector<String> Pasteboard::typesForLegacyUnsafeBindings(){NO_CLIPBOARD;}
String Pasteboard::readOrigin(){NO_CLIPBOARD;}
String Pasteboard::readString(const String&){NO_CLIPBOARD;}
String Pasteboard::readStringInCustomData(const String&){NO_CLIPBOARD;}
void Pasteboard::writeString(const String&,const String&){NO_CLIPBOARD;}
void Pasteboard::clear(){NO_CLIPBOARD;}
void Pasteboard::clear(const String&){NO_CLIPBOARD;}
void Pasteboard::read(PasteboardPlainText&,PlainTextURLReadingPolicy,std::optional<size_t>){NO_CLIPBOARD;}
void Pasteboard::read(PasteboardWebContentReader&,WebContentReadingPolicy,std::optional<size_t>){NO_CLIPBOARD;}
void Pasteboard::read(PasteboardFileReader&,std::optional<size_t>){NO_CLIPBOARD;}
void Pasteboard::write(const Color&){NO_CLIPBOARD;}
void Pasteboard::write(const PasteboardURL&){NO_CLIPBOARD;}
void Pasteboard::writeTrustworthyWebURLsPboardType(const PasteboardURL&){NO_CLIPBOARD;}
void Pasteboard::write(const PasteboardImage&){NO_CLIPBOARD;}
void Pasteboard::write(const PasteboardBuffer&){NO_CLIPBOARD;}
void Pasteboard::write(const PasteboardWebContent&){NO_CLIPBOARD;}
void Pasteboard::writeCustomData(const Vector<PasteboardCustomData>&){NO_CLIPBOARD;}
Pasteboard::FileContentState Pasteboard::fileContentState(){NO_CLIPBOARD;}
bool Pasteboard::canSmartReplace(){NO_CLIPBOARD;}
void Pasteboard::writeMarkup(const String&){NO_CLIPBOARD;}
void Pasteboard::writePlainText(const String&,SmartReplaceOption){NO_CLIPBOARD;}
void Editor::writeSelectionToPasteboard(Pasteboard&){NO_CLIPBOARD;}
void Editor::writeImageToPasteboard(Pasteboard&,Element&,const URL&,const String&){NO_CLIPBOARD;}
void Editor::platformCopyFont(){NO_CLIPBOARD;}
void Editor::platformPasteFont(){NO_CLIPBOARD;}
void Editor::pasteWithPasteboard(Pasteboard*,OptionSet<PasteOption>){NO_CLIPBOARD;}
#undef NO_CLIPBOARD

#define NO_COOKIE unavailable("Page: unexpected cookie service")
std::pair<String,bool> NetworkStorageSession::cookiesForDOM(const URL&,const SameSiteInfo&,const URL&,std::optional<FrameIdentifier>,std::optional<PageIdentifier>,IncludeSecureCookies,ApplyTrackingPrevention,ShouldRelaxThirdPartyCookieBlocking,IsKnownCrossSiteTracker) const {NO_COOKIE;}
void NetworkStorageSession::setCookiesFromDOM(const URL&,const SameSiteInfo&,const URL&,std::optional<FrameIdentifier>,std::optional<PageIdentifier>,ApplyTrackingPrevention,RequiresScriptTrackingPrivacy,const String&,ShouldRelaxThirdPartyCookieBlocking,IsKnownCrossSiteTracker) const {NO_COOKIE;}
std::pair<String,bool> NetworkStorageSession::cookieRequestHeaderFieldValue(const URL&,const SameSiteInfo&,const URL&,std::optional<FrameIdentifier>,std::optional<PageIdentifier>,IncludeSecureCookies,ApplyTrackingPrevention,ShouldRelaxThirdPartyCookieBlocking,IsKnownCrossSiteTracker) const {NO_COOKIE;}
bool NetworkStorageSession::getRawCookies(const URL&,const SameSiteInfo&,const URL&,std::optional<FrameIdentifier>,std::optional<PageIdentifier>,ApplyTrackingPrevention,ShouldRelaxThirdPartyCookieBlocking,Vector<Cookie>&) const {NO_COOKIE;}
void NetworkStorageSession::setCookie(const Cookie&){NO_COOKIE;}
void NetworkStorageSession::deleteCookie(const URL&,const URL&,const String&,CompletionHandler<void()>&&) const {NO_COOKIE;}
#undef NO_COOKIE

// Accessibility is disabled in this diagnostic's engine profile. A platform
// accessibility request therefore indicates an unexpected execution path.
#define NO_AX unavailable("Page: unexpected platform accessibility")
AccessibilityObjectInclusion AccessibilityObject::accessibilityPlatformIncludesObject() const {NO_AX;}
bool AccessibilityObject::accessibilityIgnoreAttachment() const {NO_AX;}
void AccessibilityObject::detachPlatformWrapper(AccessibilityDetachmentType){NO_AX;}
void AXObjectCache::handleScrolledToAnchor(const Node&){NO_AX;}
void AXObjectCache::nodeTextChangePlatformNotification(AccessibilityObject*,AXTextChange,unsigned,const String&){NO_AX;}
void AXObjectCache::postPlatformNotification(AccessibilityObject&,AXNotification){NO_AX;}
void AXObjectCache::attachWrapper(AccessibilityObject&){NO_AX;}
void AXObjectCache::platformHandleFocusedUIElementChanged(AccessibilityObject*,AccessibilityObject*){NO_AX;}
void AXObjectCache::frameLoadingEventPlatformNotification(RenderView*,AXLoadingEvent){NO_AX;}
void AXObjectCache::platformPerformDeferredCacheUpdate(){NO_AX;}
#undef NO_AX
}
