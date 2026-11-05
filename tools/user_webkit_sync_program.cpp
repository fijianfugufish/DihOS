#include "sacx_user_api.h"
#include <wtf/ThreadingPrimitives.h>
#include <wtf/ApproximateTime.h>
#include <wtf/ContinuousTime.h>
#include <wtf/ContinuousApproximateTime.h>
#include <wtf/MonotonicTime.h>
#include <wtf/UnbarrieredMonotonicTime.h>
#include <wtf/ProcessID.h>
#include <wtf/FileHandle.h>
#include <wtf/MappedFileData.h>
#include <wtf/MainThread.h>
#include <wtf/PageBlock.h>
#include <wtf/CPUTime.h>
#include <wtf/MemoryFootprint.h>
#include <cstring>
namespace WTF {
void initializeMainThreadPlatform();
const char* currentSearchLocaleID();
const char* currentTextBreakLocaleID();
}
static WTF::Mutex mutex;
static WTF::ThreadCondition condition;
static WTF::ThreadCondition readyCondition;
static unsigned ready, finished;
static bool releaseWorkers;
static WTF::ThreadSpecificKey key;
static uint32_t destructed;
static WTF::ProcessID processId;
static void check(bool ok) { if (!ok) sacx_user_exit(188, "WebKit synchronization fixture failed"); }
static void checkFileDenial()
{
    namespace FS = WTF::FileSystemImpl;
    FS::FileHandle empty;
    auto foreign = FS::FileHandle::adopt(123);
    check(!empty && !foreign);
    check(empty.platformHandle() == FS::invalidPlatformFileHandle);
    check(foreign.platformHandle() == FS::invalidPlatformFileHandle);
    uint8_t bytes[] { 0x11, 0x22, 0x33 };
    check(!foreign.read(bytes) && !foreign.write(bytes));
    check(bytes[0] == 0x11 && bytes[1] == 0x22 && bytes[2] == 0x33);
    check(!foreign.read(std::span<uint8_t>()) && !foreign.write(std::span<const uint8_t>()));
    check(!foreign.seek(0, FS::FileSeekOrigin::Beginning));
    check(!foreign.size() && !foreign.id());
    check(!foreign.truncate(0) && !foreign.flush());
    check(!foreign.map(FS::MappedFileMode::Private));
    FS::FileHandle moved(std::move(foreign));
    check(!moved && !foreign);
    empty = std::move(moved);
    check(!empty && !moved);
    FS::MappedFileData invalid;
    check(!invalid && !invalid.size() && invalid.span().empty());
    auto zero = FS::MappedFileData::emptyFile();
    check(!!zero && !zero.size() && zero.span().empty() && zero.mutableSpan().empty());
}
static void destroyValue(void* value)
{
    check(value != nullptr);
    __atomic_fetch_add(&destructed, 1, __ATOMIC_RELAXED);
}
static uint64_t worker(uint64_t value)
{
    check(!WTF::isMainThread());
    check(WTF::CPUTime::get().has_value() && WTF::CPUTime::forCurrentThread().value()>0);
    check(WTF::getCurrentProcessID()==processId);
    WTF::threadSpecificSet(key, reinterpret_cast<void*>(value));
    mutex.lock();
    check(WTF::threadSpecificGet(key) == reinterpret_cast<void*>(value));
    ++ready;
    readyCondition.signal();
    while (!releaseWorkers)
        check(condition.timedWait(mutex, WTF::WallTime::infinity()));
    ++finished;
    mutex.unlock();
    return value + 100;
}
extern "C" int dihos_user_main(const sacx_api* api)
{
    check(!WTF::isMainThread());
    WTF::initializeMainThreadPlatform();
    WTF::initializeMainThreadPlatform();
    check(WTF::isMainThread() && WTF::pageSize() == 4096);
    check(!strcmp(WTF::currentSearchLocaleID(), "en_US"));
    check(!strcmp(WTF::currentTextBreakLocaleID(), "en_US"));
    check(WTF::memoryFootprint()>=1024*1024);
    check(WTF::CPUTime::get().has_value() && WTF::CPUTime::forCurrentThread().value()>0);
    processId=WTF::getCurrentProcessID();check(processId>0);
    checkFileDenial();
    check((WTF::WallTime::now()-WTF::WallTime::fromRawSeconds(0)).value()>0);
    auto monotonic=WTF::MonotonicTime::now().secondsSinceEpoch().value();
    check(WTF::ApproximateTime::now().secondsSinceEpoch().value()>=monotonic);
    check(WTF::ContinuousTime::now().secondsSinceEpoch().value()>=monotonic);
    check(WTF::ContinuousApproximateTime::now().secondsSinceEpoch().value()>=monotonic);
    check(WTF::UnbarrieredMonotonicTime::now().secondsSinceEpoch().value()>=monotonic);
    check(WTF::currentTimeInNanoseconds()>0);
#ifdef USER_WEBKIT_SYNC_KEY_INVALID
    WTF::threadSpecificKeyCreate(&key, nullptr);
    WTF::threadSpecificKeyDelete(key);
    WTF::threadSpecificSet(key, reinterpret_cast<void*>(1));
    return 1;
#endif
    check(mutex.tryLock());
    check(!mutex.tryLock());
#ifdef USER_WEBKIT_SYNC_INVALID
    // A wrong-owner unlock must terminate this process, not spin in the kernel.
    mutex.unlock();
    mutex.unlock();
    return 1;
#else
    check(!condition.timedWait(mutex, WTF::WallTime::nan()));
    check(!condition.timedWait(mutex, -WTF::WallTime::infinity()));
    check(!condition.timedWait(mutex, WTF::WallTime::fromRawSeconds(0)));
    // Expiration must return with ownership restored.
    check(!condition.timedWait(mutex, WTF::WallTime::now() + WTF::Seconds(0.015)));
    mutex.unlock();
    WTF::threadSpecificKeyCreate(&key, destroyValue);
    check(key != WTF::InvalidThreadSpecificKey);
    for (unsigned round = 0; round < 2; ++round) {
        ready = finished = 0;
        releaseWorkers = false;
        sacx_user_thread threads[3];
        mutex.lock();
        for (unsigned i = 0; i < 3; ++i) {
            int64_t handle = sacx_user_thread_create(worker, i + 1);
            check(handle > 0);
            threads[i] = handle;
        }
        while (ready != 3) {
            auto deadline = round ? WTF::WallTime::now() + WTF::Seconds(3)
                                  : WTF::WallTime::fromRawSeconds(1e100);
            check(readyCondition.timedWait(mutex, deadline));
        }
        check(!WTF::threadSpecificGet(key));
        releaseWorkers = true;
        condition.broadcast();
        mutex.unlock();
        for (unsigned i = 0; i < 3; ++i) {
            uint64_t result = 0;
            check(!sacx_user_thread_join(threads[i], &result, 3000));
            check(result == i + 101);
        }
        check(finished == 3 && destructed == (round + 1) * 3);
    }
    auto old = key;
    WTF::threadSpecificKeyDelete(key);
    WTF::threadSpecificKeyCreate(&key, nullptr);
    check(key != old && !WTF::threadSpecificGet(key));
    WTF::threadSpecificKeyDelete(key);
    api->log("WebKit sync: native mutex, condition timeout/broadcast, TLS keys and cleanup OK");
    return 95;
#endif
}
