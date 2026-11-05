// Native script-engine platform policy; no pthread or host locale dependency.
#include "config.h"
#include <wtf/MainThread.h>
#include <wtf/WTFConfig.h>
#include <wtf/threads/Signals.h>
#include <wtf/PageBlock.h>
#include <wtf/WTFProcess.h>
#include <wtf/SafeStrerror.h>
#include <wtf/Language.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>
#include "sacx_user_api.h"
#include "sacx_user_threads.h"

namespace WTF {
// There is no process-signal service in the SACX profile. Initializing an empty
// registry is valid; installing handlers must fail instead of claiming delivery.
void SignalHandlers::initialize()
{
    auto& state=g_wtfConfig.signalHandlers;
    if(state.initState!=InitState::Uninitialized)
        sacx_user_exit(197,"WTF: signal registry initialized twice");
    state.initState=InitState::Initializing;
}
void SignalHandlers::finalize()
{
    Config::AssertNotFrozenScope scope;
    auto& state=g_wtfConfig.signalHandlers;
    if(state.initState!=InitState::Initializing)
        sacx_user_exit(197,"WTF: invalid signal registry finalization");
    for(auto count:state.numberOfHandlers)
        if(count)sacx_user_exit(197,"WTF: native signal delivery is unavailable");
    state.initState=InitState::Finalized;
}
void SignalHandlers::add(Signal,SignalHandler&&)
{
    sacx_user_exit(197,"WTF: native signal delivery is unavailable");
}
// The thread that first initializes WebKit is its main thread. It need not be
// the OS process's initial thread. Publish its generation-tagged native ID.
static uint64_t nativeMainThread;
void initializeMainThreadPlatform()
{
    auto current = sacx_user_thread_self();
    if (!current || static_cast<int64_t>(current) < 0)
        sacx_user_exit(194, "WTF: native thread identity unavailable");
    uint64_t expected = 0;
    if (!__atomic_compare_exchange_n(&nativeMainThread, &expected, current, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) && expected != current)
        sacx_user_exit(194, "WTF: main-thread initialization changed threads");
}
bool isMainThread()
{
    auto initialized = __atomic_load_n(&nativeMainThread, __ATOMIC_ACQUIRE);
    return initialized && initialized == sacx_user_thread_self();
}
// SACX VM ABI v1 uses 4 KiB pages on every supported native/VM target.
size_t pageSize() { static_assert(CeilingOnPageSize >= 4096); return 4096; }
// Both native WTF termination paths stop the whole isolated process. They do
// not run arbitrary user callbacks while peer threads may still hold locks.
void exitProcess(int status) { sacx_user_exit(status, nullptr); }
void terminateProcess(int status) { sacx_user_exit(status, nullptr); }
CString safeStrerror(int error)
{
    // Preserve the actual numeric native error; do not invent POSIX messages.
    return makeString("DihOS error "_s, error).utf8();
}
// DihOS currently has no user-selectable locale service. Use an explicit,
// stable browser default, shared by language preference and ICU boundaries.
const char* currentSearchLocaleID() { return "en_US"; }
const char* currentTextBreakLocaleID() { return "en_US"; }
Vector<String> platformUserPreferredLanguages(ShouldMinimizeLanguages)
{
    return { "en-US"_s };
}
}
