#include "config.h"
#include "DatabaseProviderDihOS.h"
#include "DatabaseProvider.h"
#include "IDBConnectionToServer.h"
#include "IDBDatabaseNameAndVersion.h"
#include "IDBError.h"
#include "IDBIndexIdentifier.h"
#include "IDBObjectStoreIdentifier.h"
#include <pal/SessionID.h>
#include <wtf/HashMap.h>
#include <wtf/RefCounted.h>

namespace WebCore {
// The pinned delegate's signatures are staged from EmptyClients.cpp. Its
// connection ID is real and stable, but it never advertises a working server.
#include "UnavailableIDBDelegate.inc"

class DihOSDatabaseProvider final : public DatabaseProvider {
public:
    DihOSDatabaseProvider() = default;
private:
    struct Session {
        Ref<DihOSUnavailableIDBDelegate> delegate { DihOSUnavailableIDBDelegate::create() };
        Ref<IDBClient::IDBConnectionToServer> connection;
        explicit Session(PAL::SessionID id)
            : connection(IDBClient::IDBConnectionToServer::create(delegate.get(),id))
        {
            // Use WebCore's own invalid-connection path: open/delete requests
            // receive errors later; enumeration completes with an empty list.
            // Returning a generated ID alone would leave requests pending forever.
            connection->connectionToServerLost(IDBError::serverConnectionLostError());
        }
    };
    IDBClient::IDBConnectionToServer& idbConnectionToServerForSession(PAL::SessionID id) final
    {
        auto result=m_sessions.ensure(id,[&]{return std::make_unique<Session>(id);});
        return result.iterator->value->connection.get();
    }
    HashMap<PAL::SessionID,std::unique_ptr<Session>> m_sessions;
};
}
namespace DihOSWeb {
Ref<WebCore::DatabaseProvider> createDatabaseProvider()
{
    return adoptRef(*new WebCore::DihOSDatabaseProvider);
}
}
