#pragma once
#include "navigation_client.h"
struct sacx_api;
namespace WebCore { class Page; class LocalFrame; }
namespace DihosNavigation {
int runNavigationPage(WebCore::LocalFrame&,WebCore::Page&,const sacx_api*,NavigationStatus&);
int runEmbeddedBrowser(WebCore::LocalFrame&,WebCore::Page&,const sacx_api*,NavigationStatus&);
}
