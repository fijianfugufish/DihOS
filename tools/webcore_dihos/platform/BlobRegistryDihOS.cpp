#include "config.h"
#include "BlobRegistryDihOS.h"
#include "BlobRegistry.h"
#include "BlobRegistryImpl.h"
#include "BlobDataFileReference.h"
#include "PolicyContainer.h"
#include <wtf/CompletionHandler.h>

namespace DihOSWeb {
using namespace WebCore;using namespace WTF;
// WebCore owns Blob parts, URL reference counts, revocation and top-origin
// partition checks. This process has no filesystem grant: never turn a Blob
// into a host file or accept a file-backed fallback.
class MemoryBlobRegistry final:public BlobRegistry {
public:
 void registerInternalFileBlobURL(const URL&,Ref<BlobDataFileReference>&&,const String&,const String&) final { }
 void registerInternalBlobURL(const URL& url,Vector<BlobPart>&& parts,const String& type) final {
  m_registry.registerInternalBlobURL(url,WTF::move(parts),type);
 }
 void registerBlobURL(const URL& url,const URL& source,const PolicyContainer& policy,const std::optional<SecurityOriginData>& top) final {
  m_registry.registerBlobURL(url,source,policy,top);
 }
 void registerInternalBlobURLOptionallyFileBacked(const URL& url,const URL& source,RefPtr<BlobDataFileReference>&&,const String& type) final {
  m_registry.registerInternalBlobURLOptionallyFileBacked(url,source,nullptr,type,PolicyContainer{});
 }
 void registerInternalBlobURLForSlice(const URL& url,const URL& source,long long start,long long end,const String& type) final {
  m_registry.registerInternalBlobURLForSlice(url,source,start,end,type);
 }
 void unregisterBlobURL(const URL& url,const std::optional<SecurityOriginData>& top) final {m_registry.unregisterBlobURL(url,top);}
 void registerBlobURLHandle(const URL& url,const std::optional<SecurityOriginData>& top) final {m_registry.registerBlobURLHandle(url,top);}
 void unregisterBlobURLHandle(const URL& url,const std::optional<SecurityOriginData>& top) final {m_registry.unregisterBlobURLHandle(url,top);}
 String blobType(const URL& url) final {return m_registry.blobType(url);}
 unsigned long long blobSize(const URL& url) final {return m_registry.blobSize(url);}
 void writeBlobsToTemporaryFilesForIndexedDB(const Vector<String>&,CompletionHandler<void(Vector<String>&&)>&& done) final {done({});}
 BlobRegistryImpl* blobRegistryImpl() final {return &m_registry;}
private:BlobRegistryImpl m_registry;
};
BlobRegistry* createMemoryBlobRegistry(){return new MemoryBlobRegistry;}
}
