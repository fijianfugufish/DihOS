#pragma once
// Bounded, process-owned cookie storage. Callers supply canonical ASCII URL
// hosts and WebCore's schemeful SameSite/credential decisions, not raw URLs.
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace DihosCookies {
bool publicSuffix(std::string_view);
std::string registrableDomain(std::string_view);
struct Context {std::string host,path;bool secure{},sameSite{},topLevel{},safeMethod{};int64_t now{};};
enum class SameSite {None,Lax,Strict};
struct Entry {std::string name,value,domain,path;bool hostOnly{true},secure{},httpOnly{};SameSite sameSite{SameSite::Lax};std::optional<int64_t> expiry;uint64_t order{};};
struct Header {std::string text;bool secure{},overflow{};};
inline std::string lower(std::string_view s){std::string r(s);for(char& c:r)if(c>='A'&&c<='Z')c+=32;return r;}
inline std::string_view trim(std::string_view s){while(!s.empty()&&(s.front()==' '||s.front()=='\t'))s.remove_prefix(1);while(!s.empty()&&(s.back()==' '||s.back()=='\t'))s.remove_suffix(1);return s;}
inline bool domainMatch(std::string_view host,std::string_view domain){return host==domain||(host.size()>domain.size()&&host[host.size()-domain.size()-1]=='.'&&host.substr(host.size()-domain.size())==domain);}
inline bool pathMatch(std::string_view url,std::string_view path){return url==path||(url.size()>path.size()&&url.substr(0,path.size())==path&&(path.back()=='/'||url[path.size()]=='/'));}
inline bool ipHost(std::string_view h){if(h.find(':')!=h.npos)return true;return !h.empty()&&h.find_first_not_of("0123456789.")==h.npos;}
inline bool dnsName(std::string_view s){if(s.empty()||s.size()>253||s.front()=='.'||s.back()=='.')return false;size_t n=0;for(unsigned char c:s){if(c=='.'){if(!n||n>63)return false;n=0;}else {if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return false;++n;}}return n&&n<=63;}
inline bool token(unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||std::string_view("!#$%&'*+-.^_`|~").find(c)!=std::string_view::npos;}
inline std::optional<unsigned> number(std::string_view s,unsigned limit){if(s.empty())return {};unsigned n=0;for(unsigned char c:s){if(c<'0'||c>'9'||n>limit/10||n*10>limit-(c-'0'))return {};n=n*10+c-'0';}return n;}
inline std::optional<int64_t> cookieDate(std::string_view value){
 int day=-1,month=-1,year=-1,hour=-1,minute=-1,second=-1;
 auto delimiter=[](unsigned char c){return c==9||(c>=32&&c<=47)||(c>=59&&c<=64)||(c>=91&&c<=96)||(c>=123&&c<=126);};
 for(size_t at=0;at<value.size();){
  while(at<value.size()&&delimiter(value[at]))++at;size_t begin=at;while(at<value.size()&&!delimiter(value[at]))++at;
  auto part=value.substr(begin,at-begin);if(part.empty())continue;
  if(hour<0){auto colon=part.find(':');auto next=colon==part.npos?part.npos:part.find(':',colon+1);
   if(colon!=part.npos&&next!=part.npos){auto a=number(part.substr(0,colon),23),b=number(part.substr(colon+1,next-colon-1),59),c=number(part.substr(next+1),59);if(a&&b&&c){hour=*a;minute=*b;second=*c;continue;}}
  }
  if(day<0&&part.size()<=2){auto n=number(part,31);if(n&&*n){day=*n;continue;}}
  if(month<0&&part.size()>=3){auto m=lower(part.substr(0,3));std::string_view names="janfebmaraprmayjunjulaugsepoctnovdec";for(int i=0;i<12;++i)if(names.substr(i*3,3)==m){month=i+1;break;}if(month>0)continue;}
  if(year<0&&part.size()>=2&&part.size()<=4){auto n=number(part,9999);if(n)year=*n;}
 }
 if(year>=70&&year<=99)year+=1900;else if(year>=0&&year<=69)year+=2000;
 if(year<1601||month<1||day<1||hour<0)return {};
 const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};bool leap=year%4==0&&(year%100!=0||year%400==0);
 if(day>days[month-1]+(month==2&&leap))return {};
 // Gregorian civil date to Unix days; all arithmetic fits for years 1601..9999.
 int y=year-(month<=2),era=y/400;unsigned yo=y-era*400,mp=month+(month>2?-3:9),doy=(153*mp+2)/5+day-1;
 int64_t unixDays=(int64_t)era*146097+yo*365+yo/4-yo/100+doy-719468;
 return unixDays*86400+hour*3600+minute*60+second;
}
class Store {
public:
 static constexpr size_t maxCookies=128,maxField=4096;
 bool set(const Context& c,std::string_view field,bool fromHTTP){
  if(field.empty()||field.size()>maxField||c.host.empty()||c.path.empty()||c.now<0||c.now>253402300799LL)return false;
  for(unsigned char ch:field)if((ch<32&&ch!=9)||ch==127)return false;
  auto semi=field.find(';');auto pair=trim(field.substr(0,semi));auto eq=pair.find('=');if(eq==pair.npos)return false;
  auto name=trim(pair.substr(0,eq)),value=trim(pair.substr(eq+1));if(name.empty())return false;for(unsigned char ch:name)if(!token(ch))return false;
  auto storedValue=value;
  if(value.size()>=2&&value.front()=='"'&&value.back()=='"'){value.remove_prefix(1);value.remove_suffix(1);}
  for(unsigned char ch:value)if(ch<33||ch>126||ch=='"'||ch==','||ch==';'||ch=='\\')return false;
  Entry e;e.name=name;e.value=storedValue;e.domain=lower(c.host);size_t slash=c.path.rfind('/');e.path=slash==c.path.npos||slash==0?"/":c.path.substr(0,slash);
  bool domainAttribute=false,rootPath=false;std::optional<int64_t> expires,age;
  while(semi!=field.npos){size_t start=semi+1;semi=field.find(';',start);auto attr=trim(field.substr(start,semi==field.npos?field.size()-start:semi-start));eq=attr.find('=');auto key=lower(trim(attr.substr(0,eq)));auto val=eq==attr.npos?std::string_view{}:trim(attr.substr(eq+1));
   if(key=="secure")e.secure=true;else if(key=="httponly")e.httpOnly=true;
   else if(key=="partitioned")return false; // Never ignore a required partition boundary.
   else if(key=="domain"){
    domainAttribute=true;auto d=lower(val);if(!d.empty()&&d.front()=='.')d.erase(0,1);
    if(!dnsName(d)||ipHost(c.host)||!domainMatch(e.domain,d)||publicSuffix(d))return false;
    e.domain=std::move(d);e.hostOnly=false;
   }else if(key=="path"){if(!val.empty()&&val.front()=='/'){e.path=val;rootPath=val=="/";}}
   else if(key=="samesite"){auto v=lower(val);e.sameSite=v=="none"?SameSite::None:v=="strict"?SameSite::Strict:SameSite::Lax;}
   else if(key=="expires")expires=cookieDate(val);
   else if(key=="max-age"){
    bool negative=!val.empty()&&val.front()=='-';auto digits=negative?val.substr(1):val;
    bool valid=!digits.empty();int64_t n=0;for(unsigned char ch:digits){if(ch<'0'||ch>'9'){valid=false;break;}n=std::min<int64_t>(34560000,n*10+ch-'0');}
    if(valid)age=negative?-n:n;
   }
  }
  if((e.secure&&!c.secure)||(e.httpOnly&&!fromHTTP)||(e.sameSite==SameSite::None&&!e.secure))return false;
  if(!c.sameSite&&!c.topLevel&&e.sameSite!=SameSite::None)return false;
  auto lowerName=lower(name);
  auto prefix=[&](std::string_view p){auto key=lower(p);return lowerName.substr(0,key.size())==key;};
  if(prefix("__Secure-")&&(!c.secure||!e.secure))return false;
  if(prefix("__Host-")&&(!c.secure||!e.secure||domainAttribute||!rootPath))return false;
  if((prefix("__Http-")||prefix("__Host-Http-"))&&(!fromHTTP||!c.secure||!e.secure||!e.httpOnly))return false;
  if(age){if(*age<=0)e.expiry=0;else {if(c.now<=0)return false;e.expiry=c.now+*age;}}
  else if(expires){if(c.now<=0)return false;e.expiry=std::min<int64_t>(*expires,c.now+34560000);}
  std::lock_guard<std::mutex> guard(lock);expire(c.now);
  for(const auto& old:entries){
   if(!fromHTTP&&old.httpOnly&&old.name==e.name&&old.domain==e.domain&&old.path==e.path)return false;
   if(!c.secure&&old.secure&&old.name==e.name&&(domainMatch(old.domain,e.domain)||domainMatch(e.domain,old.domain)))return false;
  }
  auto it=std::find_if(entries.begin(),entries.end(),[&](const Entry& old){return old.name==e.name&&old.domain==e.domain&&old.path==e.path;});
  if(e.expiry&&*e.expiry<=c.now){if(it!=entries.end())entries.erase(it);return true;}
  if(it!=entries.end()){e.order=it->order;*it=std::move(e);return true;}
  if(entries.size()>=maxCookies)entries.erase(std::min_element(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.order<b.order;}));
  e.order=++sequence;entries.push_back(std::move(e));return true;
 }
 Header header(const Context& c,bool includeHttpOnly,size_t limit){
  std::lock_guard<std::mutex> guard(lock);expire(c.now);std::vector<const Entry*> selected;
  for(const auto& e:entries){
   if((e.hostOnly?e.domain!=c.host:!domainMatch(c.host,e.domain))||!pathMatch(c.path,e.path)||(e.secure&&!c.secure)||(!includeHttpOnly&&e.httpOnly))continue;
   if(!c.sameSite&&(e.sameSite==SameSite::Strict||(e.sameSite==SameSite::Lax&&!(c.topLevel&&c.safeMethod))))continue;
   selected.push_back(&e);
  }
  std::sort(selected.begin(),selected.end(),[](const Entry* a,const Entry* b){return a->path.size()!=b->path.size()?a->path.size()>b->path.size():a->order<b->order;});
  Header out;for(const auto* e:selected){size_t n=e->name.size()+1+e->value.size()+(out.text.empty()?0:2);if(out.text.size()>limit||n>limit-out.text.size())return {{},false,true};if(!out.text.empty())out.text+="; ";out.text+=e->name;out.text+='=';out.text+=e->value;out.secure|=e->secure;}return out;
 }
 void clear(){std::lock_guard<std::mutex> guard(lock);entries.clear();sequence=0;}
 size_t size(){std::lock_guard<std::mutex> guard(lock);return entries.size();}
private:
 void expire(int64_t now){entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const Entry& e){return e.expiry&&now>=*e.expiry;}),entries.end());}
 std::mutex lock;std::vector<Entry> entries;uint64_t sequence{};
};
}
