#pragma once
#include <WebCore/CertificateSummary.h>
#include <optional>

namespace WebCore {
class ResourceError;
class ResourceResponse;

// The current SACX transport exports neither a certificate chain nor a peer
// verification result. Absence of this information must never mean trusted TLS.
// The native loader rejects unauthenticated HTTPS before delivering a response.
class CertificateInfo {
public:
    enum class VerificationStatus : uint8_t { Unavailable };
    CertificateInfo() = default;
    explicit CertificateInfo(const ResourceError&) { }
    explicit CertificateInfo(const ResourceResponse&) { }
    VerificationStatus verificationStatus() const { return VerificationStatus::Unavailable; }
    bool isEmpty() const { return true; }
    std::optional<CertificateSummary> summary() const { return std::nullopt; }
    CertificateInfo isolatedCopy() const { return { }; }
    bool operator==(const CertificateInfo&) const = default;
};
}
