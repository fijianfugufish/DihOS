#include "config.h"
#include "ResourceResponse.h"
#include "HTTPHeaderNames.h"
#include "HTTPParsers.h"

namespace WebCore {
String ResourceResponse::platformSuggestedFilename() const
{
    // Keep parsing/sanitization in WebCore. A server-supplied filename never
    // grants a path or bypasses the eventual scoped save-file service.
    auto disposition = httpHeaderField(HTTPHeaderName::ContentDisposition);
    return sanitizeSuggestedFilename(filenameFromHTTPContentDisposition(disposition).toString());
}
}
