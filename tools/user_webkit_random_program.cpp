#include "sacx_user_api.h"
#include <bmalloc/CryptoRandom.h>
#include <wtf/CryptographicallyRandomNumber.h>
#include <wtf/OSRandomSource.h>
#include <wtf/RandomDevice.h>
#ifndef USER_RANDOM_WTF_FIRST
#define USER_RANDOM_WTF_FIRST 0
#endif
static uint8_t a[512],b[512];
static void check(bool value){if(!value)sacx_user_exit(186,"WebKit random adapter fixture failed");}
extern "C" int dihos_user_main(const sacx_api *api){
    if(USER_RANDOM_WTF_FIRST){
        WTF::RandomDevice first;
        first.cryptographicallyRandomValues(a);
    }
    bmalloc::cryptoRandom(a,sizeof(a));WTF::cryptographicallyRandomValuesFromOS(b);
    WTF::RandomDevice device;
    device.cryptographicallyRandomValues(a);
    device.cryptographicallyRandomValues({});
    bool different=false;for(unsigned i=0;i<sizeof(a);++i)different|=a[i]!=b[i];check(different);
    (void)WTF::cryptographicallyRandomNumber<uint8_t>();
    (void)WTF::cryptographicallyRandomNumber<unsigned>();
    (void)WTF::cryptographicallyRandomNumber<uint64_t>();
    for(unsigned i=0;i<8;++i){double x=WTF::cryptographicallyRandomUnitInterval();check(x>=0 && x<1);}
    bmalloc::cryptoRandom(nullptr,0);WTF::cryptographicallyRandomValues({});
    api->log("WebKit random: native bmalloc/WTF kernel-backed buffers, integers and unit interval OK");return 94;
}
