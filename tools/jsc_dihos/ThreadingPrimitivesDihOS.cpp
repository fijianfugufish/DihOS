// Native WTF synchronization. No pthread objects, signals or kernel pointers.
#include "config.h"
#include <wtf/ThreadingPrimitives.h>
#include "sacx_user_api.h"
namespace WTF {
static void checkSync(int error)
{
    if (error)
        sacx_user_exit(187, "WTF: native synchronization failed");
}
Mutex::~Mutex()
{
    if (__atomic_load_n(&m_mutex.word, __ATOMIC_RELAXED))
        sacx_user_exit(187, "WTF: destroying a locked mutex");
}
void Mutex::lock() { checkSync(sacx_mutex_lock(&m_mutex)); }
bool Mutex::tryLock()
{
    int error = sacx_mutex_trylock(&m_mutex);
    if (error == 16)
        return false;
    checkSync(error);
    return true;
}
void Mutex::unlock() { checkSync(sacx_mutex_unlock(&m_mutex)); }
ThreadCondition::~ThreadCondition()
{
    if (__atomic_load_n(&m_condition.waiters, __ATOMIC_RELAXED))
        sacx_user_exit(187, "WTF: destroying a condition with waiters");
}
void ThreadCondition::wait(Mutex& mutex)
{
    checkSync(sacx_condition_wait(&m_condition, &mutex.impl()));
}
bool ThreadCondition::timedWait(Mutex& mutex, WallTime deadline)
{
    // Match WTF's positive-infinity wait; reject NaN and past deadlines before
    // floating-to-integer conversion. WallTime::now must be a real wall clock;
    // this backend deliberately does not substitute monotonic uptime for it.
    if (deadline.isNaN() || deadline == -WallTime::infinity())
        return false;
    if (deadline == WallTime::infinity()) {
        wait(mutex);
        return true;
    }
    double milliseconds = (deadline - WallTime::now()).value() * 1000;
    if (!(milliseconds > 0))
        return false;
    int64_t now = sacx_sync_now();
    if (now < 0)
        sacx_user_exit(187, "WTF: monotonic clock unavailable");
    // Saturate well below UINT64_MAX to avoid an overflowing/undefined cast.
    uint64_t remaining = UINT64_MAX - 1 - static_cast<uint64_t>(now);
    uint64_t ticks = remaining;
    if (milliseconds < static_cast<double>(remaining)) {
        ticks = static_cast<uint64_t>(milliseconds);
        if (static_cast<double>(ticks) < milliseconds)
            ++ticks; // never round a positive sub-millisecond wait down to zero
    }
    int error = sacx_condition_wait(&m_condition, &mutex.impl(), now + ticks);
    if (error == 110)
        return false;
    checkSync(error);
    return true;
}
void ThreadCondition::signal() { checkSync(sacx_condition_notify(&m_condition, 1)); }
void ThreadCondition::broadcast() { checkSync(sacx_condition_notify(&m_condition, 4)); }
} // namespace WTF
