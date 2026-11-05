#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "sacx_user_api.h"
extern "C" int dihos_user_main(const sacx_api *api){
    std::vector<int> values;
    for(int i=1023;i>=0;--i)values.push_back(i);
#if defined(DIHOS_LIBRARY_FAILURE_FIXTURE)
    volatile unsigned invalid=2048;volatile int result=values[invalid];(void)result;
    api->app_exit(152,"libc++ hardening did not stop invalid access");
#endif
    std::sort(values.begin(),values.end());
    std::span<const int> view(values);
    for(unsigned i=0;i<view.size();++i)if(view[i]!=static_cast<int>(i))api->app_exit(153,"libc++ vector/sort/span failed");
    std::optional<int> maybe=42;
    auto owned=std::make_unique<std::array<int,3>>();(*owned)[0]=maybe.value();
    maybe.reset();if(maybe || (*owned)[0]!=42)api->app_exit(154,"libc++ ownership/optional failed");
    auto copy=values;values.clear();
    if(copy.size()!=1024 || copy.back()!=1023)api->app_exit(155,"libc++ copy/lifetime failed");
    std::string text="DihOS";
    for(int i=0;i<100;++i)text+=" fish";
    auto text_copy=text;text.replace(0,5,"Hello");
    std::string binary("a\0b",3);
    if(text.size()!=505 || text_copy.substr(0,5)!="DihOS" || text.find("fish")!=6 || text.compare(0,5,"Hello") ||
       text.find('z')!=std::string::npos || binary.find('\0')!=1 || binary.find('b')!=2 || std::string().find('x')!=std::string::npos)
        api->app_exit(157,"libc++ string failed");
    if(api->app_yield())api->app_exit(156,"libc++ yield failed");
    api->log("SACX libc++: containers, strings, algorithms and ownership OK");return 77;
}
