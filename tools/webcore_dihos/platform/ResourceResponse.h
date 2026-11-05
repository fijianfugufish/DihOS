#pragma once
#include <WebCore/ResourceResponseBase.h>

namespace WebCore {
// All response metadata is owned by ResourceResponseBase. There is no second
// host representation to synchronize and no kernel pointer inside this type.
class ResourceResponse final : public ResourceResponseBase {
public:
    ResourceResponse() = default;
    ResourceResponse(URL&& url, String&& mimeType, long long length, String&& encoding)
        : ResourceResponseBase(WTF::move(url), WTF::move(mimeType), length, WTF::move(encoding)) { }
    ResourceResponse(ResourceResponseBase&& base) : ResourceResponseBase(WTF::move(base)) { }
    explicit ResourceResponse(std::optional<ResourceResponseData>&& data)
        : ResourceResponseBase(WTF::move(data)) { }
private:
    friend class ResourceResponseBase;
    String platformSuggestedFilename() const;
};
}
