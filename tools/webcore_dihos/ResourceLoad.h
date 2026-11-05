#pragma once
#include "DispatchQueue.h"
#include <sacx_user_network.h>
#include <wtf/WeakPtr.h>
#include <wtf/Vector.h>

namespace DihOSWeb {
// Userspace transport seam for deterministic tests and future resource schemes.
// The default implementation only communicates with the kernel through SACX.
struct ResourceTransport {
    int64_t (*start)(const char*,uint32_t,uint32_t);
    int (*poll)(uint64_t,sacx_user_http_info*);
    int (*read)(uint64_t,void*,uint32_t);
    int (*close)(uint64_t);
    int64_t (*now)();
    int (*headers)(uint64_t,void*,uint32_t,uint32_t) { nullptr };
    int64_t (*startWithHeaders)(const char*,uint32_t,const char*,uint32_t,uint32_t) { nullptr };
    int64_t (*postWithHeaders)(const char*,uint32_t,const char*,uint32_t,const void*,uint32_t,uint32_t) { nullptr };
};
class ResourceLoad final : public WTF::RefCounted<ResourceLoad>,public WTF::CanMakeWeakPtr<ResourceLoad> {
public:
    enum class State { Idle, Loading, Finished, Failed, Cancelled };
    struct Callbacks {
        WTF::Function<void(const sacx_user_http_info&)> headers;
        // Bytes are borrowed for this callback only; copy if retaining them.
        WTF::Function<void(const uint8_t*,unsigned)> data;
        WTF::Function<void(int)> complete;
    };
    static WTF::Ref<ResourceLoad> create(WTF::RunLoop&,DispatchQueue&);
    static WTF::Ref<ResourceLoad> createManual(WTF::RunLoop&,DispatchQueue&);
    static WTF::Ref<ResourceLoad> create(WTF::RunLoop&,DispatchQueue&,ResourceTransport);
    // Owner thread only. Single use. Start errors return without callbacks;
    // accepted loads deliver asynchronously. Unauthenticated TLS is denied by
    // default; the diagnostic override is not a browser security policy.
    int start(const char*,unsigned,Callbacks&&,unsigned timeoutMs=30000,bool diagnosticAllowUnverifiedTLS=false);
    int startWithHeaders(const char*,unsigned,const char*,unsigned,Callbacks&&,unsigned timeoutMs=30000);
    int startPost(const char*,unsigned,const char*,unsigned,const uint8_t*,unsigned,Callbacks&&,unsigned timeoutMs=30000);
    void setDeferred(bool);
    void cancel(); // Silent; suppresses all callbacks that have not begun.
    int copyHeaders(void*,unsigned bytes,unsigned offset);
    State state() const {return m_state;}
    int error() const {return m_error;}
    uint64_t received() const {return m_received;}
    const sacx_user_http_info& metadata() const {return m_info;}
    // Main-loop-only bounded snapshot, without query strings or credentials.
    static const char* diagnostics();
    ~ResourceLoad();
private:
    ResourceLoad(WTF::RunLoop&,DispatchQueue&,ResourceTransport);
    int schedule();
    int startInternal(const char*,unsigned,const char*,unsigned,Callbacks&&,unsigned,bool,const uint8_t* body=nullptr,unsigned bodyBytes=0,bool post=false);
    void tick();
    void deliver();
    void finish(int);
    int dispose();
    int admitQueued(uint64_t now);
    int updateTransport(uint64_t now);
    int timedOut(const char* phase);
    WTF::Ref<WTF::RunLoop> m_loop;
    WTF::Ref<DispatchQueue> m_queue;
    const ResourceTransport m_transport;
    WTF::RunLoop::Timer m_timer;
    State m_state { State::Idle };
    int m_error { };
    uint64_t m_request { },m_pending { },m_deadline { },m_received { },m_deliveryDeadline { };
    uint64_t m_networkBytes { },m_totalDeadline { };
    unsigned m_idleTimeoutMs { };
    bool m_responseObserved { };
    bool m_headersSent { },m_allowUnverified { },m_deferred { };
    sacx_user_http_info m_info { };
    uint8_t m_chunk[SACX_USER_HTTP_READ_MAX];
    Callbacks m_callbacks;
    WTF::Vector<char> m_queuedURL,m_queuedHeaders;
    WTF::Vector<uint8_t> m_queuedBody;
    bool m_post { };
    bool m_waitingForSlot { };
    ResourceLoad* m_next { };ResourceLoad* m_previous { };
    char m_diagnosticURL[128] { };
};
}
