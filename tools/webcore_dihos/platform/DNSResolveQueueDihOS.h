#pragma once

#include "DNSResolveQueue.h"
#include <wtf/CompletionHandler.h>
#include <wtf/Expected.h>

namespace WebCore {

// SACX HTTP owns transport DNS. There is no separate resolver syscall yet.
class DNSResolveQueueDihOS final : public DNSResolveQueue {
public:
    void resolve(const String&, uint64_t, DNSCompletionHandler&& completion) final
    {
        completion(makeUnexpected(DNSError::CannotResolve));
    }
    void stopResolve(uint64_t) final { } // No outstanding requests.

private:
    void updateIsUsingProxy() final { m_isUsingProxy = false; }
    void platformResolve(const String&) final { decrementRequestCount(); }
};

using DNSResolveQueuePlatform = DNSResolveQueueDihOS;

}
