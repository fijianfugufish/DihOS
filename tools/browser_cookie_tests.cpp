#include "webcore_dihos/session_cookies.h"
#include <cassert>
#include <cstdio>
using namespace DihosCookies;
int main(){
 assert(publicSuffix("com")&&publicSuffix("co.uk")&&publicSuffix("github.io")&&publicSuffix("a.ck")&&!publicSuffix("www.ck"));
 assert(registrableDomain("a.b.example.co.uk")=="example.co.uk"&&registrableDomain("a.www.ck")=="www.ck"&&registrableDomain("x.github.io")=="x.github.io");
 assert(publicSuffix("unknown")&&!publicSuffix("a.unknown")&&registrableDomain("a.unknown")=="a.unknown");
 assert(cookieDate("Thu, 01 Jan 1970 00:00:00 GMT")==0);
 assert(cookieDate("Wed, 09 Jun 2027 10:18:14 GMT")==1812536294);
 assert(!cookieDate("Wed, 31 Feb 2027 10:18:14 GMT")&&!cookieDate("Wed, 09 Jun 2027 25:18:14 GMT"));
 Context c{"www.example.com","/dir/page",true,true,false,true,1800000000};Store s;
 assert(s.set(c,"a=one; Path=/",true));assert(s.set(c,"a=two; Path=/dir",true));
 assert(s.header(c,true,100).text=="a=two; a=one");
 c.path="/directory";assert(s.header(c,true,100).text=="a=one");c.path="/dir/page";
 assert(s.set(c,"host=only; Path=/",true));auto child=c;child.host="sub.www.example.com";assert(s.header(child,true,100).text.empty());
 assert(s.set(c,"domain=shared; Domain=.example.com; Path=/",true));assert(s.header(child,true,100).text=="domain=shared");
 assert(!s.set(c,"bad=x; Domain=com",true)&&!s.set(c,"bad=x; Domain=evil.com",true));
 auto p=c;p.host="user.github.io";assert(!s.set(p,"bad=x; Domain=github.io",true));
 p.host="127.0.0.1";assert(!s.set(p,"bad=x; Domain=127.0.0.1",true));
 assert(s.set(c,"secret=hidden; Secure; HttpOnly; Path=/",true));assert(s.header(c,true,4096).text.find("secret=hidden")!=std::string::npos);
 assert(s.header(c,false,4096).text.find("secret")==std::string::npos);
 assert(!s.set(c,"secret=replaced; Path=/",false)&&!s.set(c,"secret=; Max-Age=0; Path=/",false));
 assert(!s.set(c,"fake=x; HttpOnly",false));
 auto http=c;http.secure=false;assert(!s.set(http,"secret=insecure; Path=/",true));assert(!s.set(http,"secure=x; Secure",true));
 assert(s.header(http,true,4096).text.find("secret")==std::string::npos);
 assert(!s.set(c,"none=x; SameSite=None",true));assert(s.set(c,"none=x; SameSite=None; Secure; Path=/",true));
 auto cross=c;cross.sameSite=false;assert(s.header(cross,true,4096).text=="none=x");
 cross.topLevel=true;assert(s.header(cross,true,4096).text.find("a=one")!=std::string::npos);cross.safeMethod=false;assert(s.header(cross,true,4096).text=="none=x");
 cross.topLevel=false;assert(!s.set(cross,"blocked=x; SameSite=Lax",true));assert(s.set(cross,"allowed=x; SameSite=None; Secure",true));
 assert(!s.set(c,"__Host-test=x; Secure",true)&&!s.set(c,"__Host-test=x; Secure; Path=/; Domain=example.com",true));
 assert(s.set(c,"__Host-test=x; Secure; Path=/",true));assert(!s.set(c,"__Secure-test=x",true));
 assert(!s.set(c,"__hOsT-mixed=x; Secure; Path=/; Domain=example.com",true)&&!s.set(c,"__sEcUrE-mixed=x",true));
 assert(s.set(c,"__HOST-good=x; Secure; Path=/",true));
 assert(!s.set(c,"__Http-test=x; Secure",true));assert(s.set(c,"__Http-test=x; Secure; HttpOnly",true));
 assert(!s.set(c,"part=x; Secure; SameSite=None; Partitioned",true));
 assert(!s.set(c,"bad name=x",true)&&!s.set(c,"bad=x\r\nCookie: stolen",true));
 assert(s.header(c,true,1).overflow&&s.header(c,true,1).text.empty());
 assert(s.set(c,"short=x; Max-Age=2",true));auto later=c;later.now+=3;assert(s.header(later,true,4096).text.find("short=")==std::string::npos);
 assert(s.set(c,"expires=x; Expires=Thu, 01 Jan 1970 00:00:00 GMT",true));assert(s.header(c,true,4096).text.find("expires=")==std::string::npos);
 assert(s.set(c,"override=x; Max-Age=100; Expires=Thu, 01 Jan 1970 00:00:00 GMT",true));assert(s.header(c,true,4096).text.find("override=x")!=std::string::npos);
 for(unsigned i=0;i<300;++i)assert(s.set(c,"q"+std::to_string(i)+"=v; Path=/",true));assert(s.size()==Store::maxCookies);
 s.clear();assert(!s.size()&&s.header(c,true,4096).text.empty());
 assert(s.set(c,"quoted=\"opaque\"; Path=/",true));assert(s.header(c,true,4096).text=="quoted=\"opaque\"");s.clear();
 auto unknownClock=c;unknownClock.now=0;assert(!s.set(unknownClock,"dated=x; Max-Age=30",true));
 uint32_t random=0x51623a1u;auto next=[&]{random=random*1664525u+1013904223u;return random;};
 for(unsigned i=0;i<10000;++i){
  std::string field="f"+std::to_string(i)+"=";unsigned n=next()%192;
  for(unsigned j=0;j<n;++j)field+=(char)((i&1)?(33+next()%94):(next()>>24));
  if(i%3==0)field+="; Domain=example.com; Path=/";
  (void)s.set(c,field,true);auto h=s.header(c,true,1024*1024);assert(!h.overflow&&h.text.find_first_of("\r\n")==std::string::npos&&h.text.find('\0')==std::string::npos&&s.size()<=Store::maxCookies);
  auto outsider=c;outsider.host="attacker.test";assert(s.header(outsider,true,1024*1024).text.empty());
 }
 s.clear();
 puts("PASS: official PSL, cookie parsing, host/path isolation, HttpOnly/Secure/SameSite, prefixes, expiration, bounded headers/quota and session cleanup");
 puts("PASS: 10000 deterministic malformed/valid cookie fields preserve header safety, quota and cross-domain isolation");
}
