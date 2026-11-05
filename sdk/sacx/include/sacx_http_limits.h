#pragma once
/* Request/redirect capacity, excluding NUL. Public response-info structs keep
 * their existing presentation fields and ABI layout. */
#define SACX_HTTP_URL_MAX 8191u
#define SACX_HTTP_REQUEST_HEADERS_MAX 16384u
#define SACX_HTTP_REQUEST_BODY_MAX 16384u
/* Per-response allowance, separate from upload and video-segment limits. */
#define SACX_HTTP_RESPONSE_MAX (16u*1024u*1024u)
