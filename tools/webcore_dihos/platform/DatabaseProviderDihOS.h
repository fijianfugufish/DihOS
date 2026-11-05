#pragma once
#include "DatabaseProvider.h"
#include <wtf/Ref.h>
namespace DihOSWeb {
WTF::Ref<WebCore::DatabaseProvider> createDatabaseProvider();
}
