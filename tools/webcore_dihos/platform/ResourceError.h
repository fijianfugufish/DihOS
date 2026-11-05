#pragma once
#include <WebCore/ResourceErrorBase.h>

namespace WebCore {
// Native requests carry error metadata in WebCore-owned strings. There is no
// host error object, borrowed kernel address or implicit transport authority.
class ResourceError final : public ResourceErrorBase {
public:
    ResourceError(Type type = Type::Null) : ResourceErrorBase(type) { }
    ResourceError(const String& domain, int code, const URL& url,
        const String& description, Type type = Type::General,
        IsSanitized sanitized = IsSanitized::No)
        : ResourceErrorBase(domain, code, url, description, type, sanitized) { }

    ErrorRecoveryMethod errorRecoveryMethod() const { return ErrorRecoveryMethod::NoRecovery; }

private:
    friend class ResourceErrorBase;
    void doPlatformIsolatedCopy(const ResourceError& other)
    {
        // The base copies the common strings/code/type; retain its sanitization
        // classification as well. There are no additional platform fields.
        m_isSanitized = other.m_isSanitized;
    }
};
}
