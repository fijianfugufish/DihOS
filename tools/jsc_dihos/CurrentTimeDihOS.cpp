#include "config.h"
#include <wtf/ApproximateTime.h>
#include <wtf/ContinuousApproximateTime.h>
#include <wtf/ContinuousTime.h>
#include <wtf/MonotonicTime.h>
#include <wtf/UnbarrieredMonotonicTime.h>
#include <wtf/WallTime.h>
#include "sacx_user_api.h"
namespace WTF {
static int64_t clockMilliseconds(unsigned operation)
{
    auto value=sacx_user_call(operation,0,0,0);
    if(value<0)sacx_user_exit(193,"WTF: required native clock unavailable");
    return value;
}
WallTime WallTime::now(){return fromRawSeconds(clockMilliseconds(SACX_USER_UTC_MS)/1000.0);}
Int128 currentTimeInNanoseconds(){return static_cast<Int128>(clockMilliseconds(SACX_USER_UTC_MS))*1000000;}
MonotonicTime MonotonicTime::now(){return fromRawSeconds(clockMilliseconds(SACX_USER_MONOTONIC_MS)/1000.0);}
ApproximateTime ApproximateTime::now(){return fromRawSeconds(MonotonicTime::now().secondsSinceEpoch().value());}
// DihOS currently has no guest suspend/power-state API. All elapsed-time clocks
// use the architectural monotonic service, never a clamped wall clock.
ContinuousTime ContinuousTime::now(){return fromRawSeconds(MonotonicTime::now().secondsSinceEpoch().value());}
ContinuousApproximateTime ContinuousApproximateTime::now(){return fromRawSeconds(MonotonicTime::now().secondsSinceEpoch().value());}
#if !USE(HARDWARE_UNBARRIERED_MONOTONIC_TIME)
UnbarrieredMonotonicTime UnbarrieredMonotonicTime::now(){return UnbarrieredMonotonicTime(MonotonicTime::now().secondsSinceEpoch().value());}
#endif
}
