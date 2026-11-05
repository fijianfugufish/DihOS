#include "config.h"
#include "SessionCookiesDihOS.h"
#include "session_cookies.h"
#include "Cookie.h"
#include "Document.h"
#include "SecurityOrigin.h"
#include "StorageSessionProvider.h"
#include <wtf/CompletionHandler.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/CString.h>
#include <sdk/sacx_user_syscall.h>
namespace DihOSWeb {
using namespace WebCore;using namespace WTF;
static DihosCookies::Store& store(){static NeverDestroyed<DihosCookies::Store> value;return value.get();}
static std::string bytes(StringView s){auto utf=s.toString().utf8();return {utf.data(),utf.length()};}
static String text(const std::string& s){return String::fromUTF8(std::span<const char>{s.data(),s.size()});}
static DihosCookies::Context context(const URL& url,const SameSiteInfo& site){
 auto utc=sacx_user_call(SACX_USER_UTC_MS,0,0,0);
 return {bytes(url.host()),bytes(url.path()),url.protocolIs("https"_s),site.isSameSite,site.isTopSite,site.isSafeHTTPMethod,utc>0?utc/1000:0};
}
static bool webURL(const URL& url){return url.isValid()&&url.protocolIsInHTTPFamily()&&!url.host().isEmpty();}
class NoPlatformSession final:public StorageSessionProvider {
public:NetworkStorageSession* storageSession() const final{return nullptr;}
};
class SessionJar final:public CookieJar {
public:
 SessionJar():CookieJar(adoptRef(*new NoPlatformSession)){}
 ~SessionJar(){store().clear();}
 String cookies(Document& doc,const URL& url) const final {
  if(!webURL(url)||doc.securityOrigin().isOpaque())return {};
  auto c=context(url,sameSiteInfo(doc,IsForDOMCookieAccess::Yes));c.topLevel=false;c.safeMethod=false;
  return text(store().header(c,false,DihosCookies::Store::maxCookies*(DihosCookies::Store::maxField+2)).text);
 }
 void setCookies(Document& doc,const URL& url,const String& value) final {
  if(!webURL(url)||doc.securityOrigin().isOpaque())return;
  auto c=context(url,sameSiteInfo(doc,IsForDOMCookieAccess::Yes));c.topLevel=false;c.safeMethod=false;
  store().set(c,bytes(value),false);
 }
 bool cookiesEnabled(Document& doc) final{return webURL(doc.cookieURL())&&!doc.securityOrigin().isOpaque();}
 void remoteCookiesEnabled(const Document& doc,CompletionHandler<void(bool)>&& done) const final {done(webURL(doc.cookieURL())&&!doc.securityOrigin().isOpaque());}
 std::pair<String,SecureCookiesAccessed> cookieRequestHeaderFieldValue(const URL&,const SameSiteInfo& site,const URL& url,std::optional<FrameIdentifier>,std::optional<PageIdentifier>,IncludeSecureCookies include) const final {
  if(!webURL(url))return {};
  auto c=context(url,site);if(include==IncludeSecureCookies::No)c.secure=false;
  auto h=store().header(c,true,DihosCookies::Store::maxCookies*(DihosCookies::Store::maxField+2));
  return {text(h.text),h.secure?SecureCookiesAccessed::Yes:SecureCookiesAccessed::No};
 }
 // Cookie Store API is explicitly unavailable in this first session profile.
 // These paths must never fall through to the old platform abort stubs.
 bool getRawCookies(Document&,const URL&,Vector<Cookie>& out) const final {out.clear();return false;}
 void setRawCookie(const Document&,const Cookie&,ShouldPartitionCookie) final {}
 void deleteCookie(const Document&,const URL&,const String&,CompletionHandler<void()>&& done) final {done();}
 void getCookiesAsync(Document&,const URL&,const CookieStoreGetOptions&,CompletionHandler<void(std::optional<Vector<Cookie>>&&)>&& done) const final {done(std::nullopt);}
 void setCookieAsync(Document&,const URL&,const Cookie&,CompletionHandler<void(bool)>&& done) const final {done(false);}
};
Ref<CookieJar> createSessionCookieJar(){return adoptRef(*new SessionJar);}
std::optional<String> requestCookies(const ResourceRequest& request,size_t limit){
 if(!request.allowCookies()||!webURL(request.url()))return String{};
 auto h=store().header(context(request.url(),SameSiteInfo::create(request)),true,limit);
 if(h.overflow)return {};return text(h.text);
}
void responseCookie(const URL& url,const SameSiteInfo& site,StringView field){if(webURL(url))store().set(context(url,site),bytes(field),true);}
bool cookiePublicSuffix(StringView host){
 // document.domain supplies caller-controlled spelling here. URL hostnames
 // arrive as canonical Punycode, but the domain-relaxation API may not.
 if(!host.containsOnlyASCII())return true;
 return DihosCookies::publicSuffix(DihosCookies::lower(bytes(host)));
}
String cookieRegistrableDomain(StringView host){
 if(!host.containsOnlyASCII())return host.toString();
 return text(DihosCookies::registrableDomain(DihosCookies::lower(bytes(host))));
}
void clearSessionCookies(){store().clear();}
}
