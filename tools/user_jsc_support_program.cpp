#include "sacx_user_api.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#define CHECK(value) do {if(!(value))sacx_user_exit(198,"JSC support fixture failed: " #value);}while(0)
struct ArithmeticCase {uint64_t ahi,alo,bhi,blo,qhi,qlo,rhi,rlo;};
#include "user_arithmetic_vectors.inc"
using Wide=unsigned __int128;
static Wide wide(uint64_t high,uint64_t low){return (Wide(high)<<64)|low;}
static std::string key(unsigned i){char value[]={'k',char('A'+i/26),char('a'+i%26),0};return std::string(value);}
extern "C" int dihos_user_main(const sacx_api *api){
    for(auto row:unsigned_cases){
        volatile Wide a=wide(row.ahi,row.alo),b=wide(row.bhi,row.blo);
        CHECK(a/b==wide(row.qhi,row.qlo) && a%b==wide(row.rhi,row.rlo));
    }
    for(auto row:signed_cases){
        volatile __int128 a=static_cast<__int128>(wide(row.ahi,row.alo));
        volatile __int128 b=static_cast<__int128>(wide(row.bhi,row.blo));
        CHECK(Wide(a/b)==wide(row.qhi,row.qlo) && Wide(a%b)==wide(row.rhi,row.rlo));
    }
    volatile __int128 big=(__int128(1)<<100)+(__int128(1)<<80);
    CHECK(double(big)==0x1.00001p100);
    volatile double number=-0x1.00001p100;
    CHECK(__int128(number)==-big);
    volatile long double one=1.0L,tiny=0x1p-100L;
    volatile long double divisor=2,sum=one+tiny,product=sum*sum,half=product/divisor;
    CHECK(sum-one==tiny && product-one==2*tiny && half*2==product);
    CHECK(double(sum)==1.0 && float(sum)==1.0f);
    volatile float narrow=1.5f;volatile double normal=-2.25;
    volatile long double widenedFloat=static_cast<long double>(narrow),widenedDouble=static_cast<long double>(normal);
    CHECK(widenedFloat==1.5L && widenedDouble==-2.25L);
    volatile int integer=-12345;volatile unsigned positive=12345;
    volatile long double extended=static_cast<long double>(integer);
    CHECK(extended==-12345.0L && int(extended)==integer && static_cast<long long>(extended)==integer);
    CHECK(static_cast<long double>(positive)==12345.0L && unsigned(-extended)==positive);
    {
        std::unordered_map<std::string,unsigned> map;
        for(unsigned i=0;i<400;++i)CHECK(map.emplace(key(i),i+7).second);
        map.rehash(1103);CHECK(map.size()==400 && map.bucket_count()>=1103);
        for(unsigned i=0;i<400;++i)CHECK(map.at(key(i))==i+7);
        for(unsigned i=0;i<400;i+=3)CHECK(map.erase(key(i))==1);
        CHECK(map.size()==266 && map.find(key(0))==map.end() && map.at(key(1))==8);
        std::unordered_set<uint64_t> set;
        for(unsigned i=0;i<256;++i){CHECK(set.insert(uint64_t(i)*i+17).second);CHECK(!set.insert(uint64_t(i)*i+17).second);}
        CHECK(set.size()==256);
    }
    api->log("JSC support: pinned 128-bit arithmetic, conversions and libc++ hashing OK");
    return 98;
}
