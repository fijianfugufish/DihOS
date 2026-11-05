#include <wtf/CryptographicallyRandomNumber.h>
#include <wtf/OSRandomSource.h>
#include "sacx_user_random.h"
#include "sacx_user_api.h"
namespace WTF {
void cryptographicallyRandomValues(std::span<uint8_t> buffer){
    if(sacx_user_random(buffer.data(),buffer.size()))sacx_user_exit(185,"WTF: trusted kernel entropy unavailable");
}
void cryptographicallyRandomValuesFromOS(std::span<uint8_t> buffer){cryptographicallyRandomValues(buffer);}
template<> uint8_t cryptographicallyRandomNumber<uint8_t>(){uint8_t value;cryptographicallyRandomValues({&value,1});return value;}
template<> unsigned cryptographicallyRandomNumber<unsigned>(){
    unsigned value;cryptographicallyRandomValues({reinterpret_cast<uint8_t*>(&value),sizeof(value)});return value;
}
template<> uint64_t cryptographicallyRandomNumber<uint64_t>(){
    uint64_t value;cryptographicallyRandomValues({reinterpret_cast<uint8_t*>(&value),sizeof(value)});return value;
}
double cryptographicallyRandomUnitInterval(){return cryptographicallyRandomNumber<unsigned>()/4294967296.0;}
}
