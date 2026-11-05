#include "session_cookies.h"
#include "browser_psl.inc"
namespace DihosCookies {
static bool rule(std::string_view name){return std::binary_search(std::begin(pslRules),std::end(pslRules),name);}
static std::string_view suffix(std::string_view host){
 if(host.empty()||ipHost(host))return {};
 size_t best=host.rfind('.');best=best==host.npos?0:best+1;
 for(size_t at=0;at<host.size();){
  auto part=host.substr(at);auto dot=part.find('.');
  if(rule("!"+std::string(part)))return dot==part.npos?std::string_view{}:part.substr(dot+1);
  if(rule(part)||(dot!=part.npos&&rule("*."+std::string(part.substr(dot+1)))))best=std::min(best,at);
  if(dot==part.npos)break;at+=dot+1;
 }
 return host.substr(best);
}
bool publicSuffix(std::string_view host){auto s=suffix(host);return !s.empty()&&host==s;}
std::string registrableDomain(std::string_view host){auto s=suffix(host);if(s.empty()||s==host)return {};size_t at=host.size()-s.size()-1;size_t dot=at?host.rfind('.',at-1):host.npos;return std::string(host.substr(dot==host.npos?0:dot+1));}
}
