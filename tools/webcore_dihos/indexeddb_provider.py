"""Stage the pinned IDB delegate ABI without patching shared engine archives."""
def unavailable_delegate(text):
    start=text.index('    struct EmptyIDBConnectionToServerDeletegate final')
    end=text.index('\n    IDBClient::IDBConnectionToServer& idbConnectionToServerForSession',start)
    delegate=text[start:end].replace('EmptyIDBConnectionToServerDeletegate','DihOSUnavailableIDBDelegate')
    old='std::optional<IDBConnectionIdentifier> NODELETE identifier() const final { return std::nullopt; }'
    assert delegate.count(old)==1
    delegate=delegate.replace(old,'std::optional<IDBConnectionIdentifier> NODELETE identifier() const final { return m_identifier; }')
    constructor='DihOSUnavailableIDBDelegate() = default;'
    assert delegate.count(constructor)==1
    delegate=delegate.replace(constructor,constructor+'\n        const IDBConnectionIdentifier m_identifier { IDBConnectionIdentifier::generate() };')
    # Retain the upstream licence on the generated derivative.
    licence=text[:text.index('#include "config.h"')]
    return licence+'// Generated from the pinned EmptyClients.cpp delegate interface.\n'+delegate+'\n'
