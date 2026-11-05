#pragma once
#include "CookieJar.h"
#include "ResourceRequest.h"
#include <wtf/text/WTFString.h>
#include <wtf/URL.h>
#include <optional>
namespace DihOSWeb {
WTF::Ref<WebCore::CookieJar> createSessionCookieJar();
std::optional<WTF::String> requestCookies(const WebCore::ResourceRequest&,size_t limit);
void responseCookie(const WTF::URL&,const WebCore::SameSiteInfo&,WTF::StringView);
bool cookiePublicSuffix(WTF::StringView);
WTF::String cookieRegistrableDomain(WTF::StringView);
void clearSessionCookies();
}
