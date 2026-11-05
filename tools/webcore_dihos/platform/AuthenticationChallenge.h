#pragma once
#include <WebCore/AuthenticationChallengeBase.h>
#include <WebCore/AuthenticationClient.h>
#include <wtf/RefPtr.h>

namespace WebCore {
// Canonical challenge metadata, with an owned client only for a live exchange.
// Constructing this value does not authenticate or grant transport authority.
class AuthenticationChallenge final : public AuthenticationChallengeBase {
public:
    AuthenticationChallenge() = default;
    AuthenticationChallenge(const ProtectionSpace& space, const Credential& credential,
        unsigned failures, const ResourceResponse& response, const ResourceError& error)
        : AuthenticationChallengeBase(space, credential, failures, response, error) { }
    AuthenticationChallenge(const ProtectionSpace& space, const Credential& credential,
        unsigned failures, const ResourceResponse& response, const ResourceError& error,
        Ref<AuthenticationClient>&& client)
        : AuthenticationChallengeBase(space, credential, failures, response, error)
        , m_client(WTF::move(client)) { }
    AuthenticationClient* authenticationClient() const
    { RELEASE_ASSERT(m_client); return m_client.get(); }
    void setProposedCredential(const Credential& credential) { m_proposedCredential = credential; }
private:
    friend class AuthenticationChallengeBase;
    static bool platformCompare(const AuthenticationChallenge& a, const AuthenticationChallenge& b)
    { return a.m_client == b.m_client; }
    RefPtr<AuthenticationClient> m_client;
};
}
