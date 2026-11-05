#include "config.h"
#include <wtf/Threading.h>
#include "sacx_user_api.h"
#include "sacx_user_thread_context.h"

namespace WTF {
Thread::~Thread() = default;
[[noreturn]] static void nativeThreadFailure(){sacx_user_exit(192,"WTF: native thread contract failed");}
static uint64_t nativeEntry(uint64_t context)
{
    Thread::entryPoint(reinterpret_cast<Thread::NewThreadContext*>(context));
    return 0;
}
void Thread::initializePlatformThreading()
{
    sacx_user_info info;
    if(sacx_user_call(SACX_USER_QUERY,reinterpret_cast<uintptr_t>(&info),sizeof(info),0)
        || !(info.capabilities&SACX_USER_CAP_THREADS) || (info.operations&0x7c0000)!=0x7c0000)
        nativeThreadFailure();
}
void Thread::initializeCurrentThreadEvenIfNonWTFCreated() { }
void Thread::initializeCurrentThreadInternal(const char*) { }
bool Thread::establishHandle(NewThreadContext& context,StackAllocationSpecification spec,QOS,SchedulingPolicy policy)
{
    // Custom/preallocated stacks and real-time scheduling need distinct kernel
    // contracts. Do not silently accept unsupported requests.
    if(policy==SchedulingPolicy::Realtime || spec.kind()==StackAllocationSpecification::Kind::SizeAndLocation)
        return false;
    if(spec.kind()==StackAllocationSpecification::Kind::SizeOnly && spec.sizeBytes()>1024*1024)
        return false;
    auto handle=sacx_user_thread_create(nativeEntry,reinterpret_cast<uintptr_t>(&context));
    if(handle<0)return false;
    establishPlatformSpecificHandle(handle,handle);
    return true;
}
void Thread::establishPlatformSpecificHandle(PlatformThreadHandle handle,ThreadIdentifier)
{
    Locker locker { m_mutex };m_handle=handle;
}
void Thread::initializeSchedulingAttributes() { }
void Thread::updateSchedulingAttributes(SchedulingState) const { }
void Thread::changePriority(int) { } // advisory QoS; native scheduler is round-robin
void Thread::yield(){if(sacx_user_call(SACX_USER_YIELD,0,0,0))nativeThreadFailure();}
int Thread::waitForCompletion()
{
    PlatformThreadHandle handle;
    { Locker locker { m_mutex };handle=m_handle; }
    int result;
    do { result=sacx_user_thread_join(handle,nullptr,60000); } while(result==-110);
    if(result)return -result;
    Locker locker { m_mutex };
    if(!hasExited())didJoin();
    return 0;
}
void Thread::detach()
{
    Locker locker { m_mutex };
    if(sacx_user_thread_detach(m_handle))nativeThreadFailure();
    if(!hasExited())didBecomeDetached();
}
Thread& Thread::initializeCurrentTLS()
{
    WTF::initialize();
    auto handle=sacx_user_thread_self();
    Ref thread=adoptRef(*new Thread(defaultQOS,SchedulingPolicy::Other,(handle&255)?IsMain::No:IsMain::Yes));
    thread->establishPlatformSpecificHandle(handle,handle);
    thread->initializeInThread();
    return initializeTLS(WTF::move(thread));
}
void Thread::initializeTLSKey(){threadSpecificKeyCreate(&s_key,destructTLS);}
Thread& Thread::initializeTLS(Ref<Thread>&& thread)
{
    auto& inTLS=thread.leakRef();threadSpecificSet(s_key,&inTLS);return inTLS;
}
void Thread::destructTLS(void* value)
{
    auto* thread=static_cast<Thread*>(value);
    if(thread->m_isDestroyedOnce){thread->didExit();thread->deref();return;}
    thread->m_isDestroyedOnce=true;
    threadSpecificSet(s_key,thread);
    thread->m_clientData=nullptr;
}
auto Thread::suspend(const ThreadSuspendLocker&) -> Expected<void,PlatformSuspendError>
{
    if(m_suspendCount==UINT_MAX)return makeUnexpected(75);
    if(!m_suspendCount){
        int error=sacx_user_thread_pause(m_handle,&m_nativeRegisters.native);
        if(error)return makeUnexpected(-error);
        m_nativeRegisters.stackPointer=reinterpret_cast<void*>(m_nativeRegisters.native.sp);
    }
    ++m_suspendCount;return { };
}
void Thread::resume(const ThreadSuspendLocker&)
{
    if(!m_suspendCount)nativeThreadFailure();
    if(!--m_suspendCount && sacx_user_thread_resume(m_handle))nativeThreadFailure();
}
size_t Thread::getRegisters(const ThreadSuspendLocker&,PlatformRegisters& registers)
{
    if(!m_suspendCount)nativeThreadFailure();
    registers=m_nativeRegisters;return sizeof(registers);
}
StackBounds StackBounds::currentThreadStackBoundsInternal()
{
    return newThreadStackBounds(sacx_user_thread_self());
}
StackBounds StackBounds::newThreadStackBounds(PlatformThreadHandle handle)
{
    sacx_user_stack_bounds bounds;
    if(sacx_user_thread_stack(handle,&bounds))nativeThreadFailure();
    return StackBounds(reinterpret_cast<void*>(bounds.end),reinterpret_cast<void*>(bounds.begin));
}
} // namespace WTF
