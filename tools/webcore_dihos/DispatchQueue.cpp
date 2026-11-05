#include "config.h"
#include "DispatchQueue.h"

namespace DihOSWeb {
DispatchQueue::Posted DispatchQueue::post(WTF::Function<void()>&& function)
{
    bool wake=false;uint64_t id;
    {
        WTF::Locker locker { m_lock };
        if(m_closed)return {0,Error::Closed};
        if(!function)return {0,Error::EmptyFunction};
        if(m_count==capacity)return {0,Error::Full};
        if(!m_nextID)return {0,Error::Exhausted};
        id=m_nextID++;
        auto& entry=m_entries[(m_head+m_count++)%capacity];
        entry.id=id;entry.cancelled=false;entry.function=WTF::move(function);
        if(!m_scheduled){m_scheduled=true;wake=true;}
    }
    if(wake)schedule();
    return {id,Error::None};
}
void DispatchQueue::schedule()
{
    m_loop->dispatch([self=WTF::Ref { *this }] { self->drain(); });
}
bool DispatchQueue::cancel(uint64_t id)
{
    if(!id)return false;
    WTF::Locker locker { m_lock };
    for(unsigned i=0;i<m_count;++i){
        auto& entry=m_entries[(m_head+i)%capacity];
        if(entry.id==id && !entry.cancelled){entry.cancelled=true;return true;}
    }
    return false;
}
unsigned DispatchQueue::pending() const
{
    WTF::Locker locker { m_lock };return m_count;
}
bool DispatchQueue::isClosed() const
{
    WTF::Locker locker { m_lock };return m_closed;
}
void DispatchQueue::drain()
{
    RELEASE_ASSERT(m_loop->isCurrent());
    for(unsigned n=0;n<turnBudget;++n){
        WTF::Function<void()> function;bool cancelled;
        {
            WTF::Locker locker { m_lock };
            if(!m_count)break;
            auto& entry=m_entries[m_head];
            function=WTF::move(entry.function);cancelled=entry.cancelled;
            entry.id=0;m_head=(m_head+1)%capacity;--m_count;
        }
        // Never hold the registry lock across callbacks or capture destruction.
        if(!cancelled)function();
    }
    bool again;
    {
        WTF::Locker locker { m_lock };again=m_count;
        if(!again)m_scheduled=false;
    }
    if(again)schedule();
}
void DispatchQueue::close()
{
    RELEASE_ASSERT(m_loop->isCurrent());
    { WTF::Locker locker { m_lock };m_closed=true; }
    while(true){
        WTF::Function<void()> function;
        {
            WTF::Locker locker { m_lock };if(!m_count)break;
            auto& entry=m_entries[m_head];function=WTF::move(entry.function);
            entry.id=0;m_head=(m_head+1)%capacity;--m_count;
        }
    }
}
DispatchQueue::~DispatchQueue()
{
    RELEASE_ASSERT(m_closed && !m_count);
}
}
