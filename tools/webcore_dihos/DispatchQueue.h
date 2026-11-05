#pragma once
#include <wtf/RunLoop.h>
#include <wtf/ThreadSafeRefCounted.h>

namespace DihOSWeb {
// Userspace scheduling only: no kernel pointers, process IPC or DOM API.
// Accepted functions execute/destroy on the owning loop. Posting never waits
// for space. Cancellation retains its slot until owner-thread disposal.
class DispatchQueue final : public WTF::ThreadSafeRefCounted<DispatchQueue> {
public:
    static constexpr unsigned capacity=32,turnBudget=8;
    enum class Error { None, Full, Closed, Exhausted, EmptyFunction };
    struct Posted { uint64_t id; Error error; };
    static WTF::Ref<DispatchQueue> create(WTF::RunLoop& loop) { return WTF::adoptRef(*new DispatchQueue(loop)); }
    Posted post(WTF::Function<void()>&&);
    bool cancel(uint64_t);
    unsigned pending() const;
    bool isClosed() const;
    void close(); // Owner thread only. Does not interrupt an executing callback.
    ~DispatchQueue();
private:
    explicit DispatchQueue(WTF::RunLoop& loop):m_loop(loop) { }
    void drain();
    void schedule();
    struct Entry { uint64_t id { }; bool cancelled { }; WTF::Function<void()> function; };
    WTF::Ref<WTF::RunLoop> m_loop;
    mutable WTF::Lock m_lock;
    Entry m_entries[capacity];
    unsigned m_head { },m_count { };
    uint64_t m_nextID { 1 };
    bool m_scheduled { },m_closed { };
};
}
