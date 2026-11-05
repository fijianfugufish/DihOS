#pragma once
#include "sacx_api.h"
#include "sacx_browser_session.h"
/* Append-only extension of the existing ABI. The base SDK layout remains
   usable by independently pinned engine builds and older applications. */
typedef struct sacx_browser_frontend_api {
 sacx_api base;
 int (*browser_session)(uint32_t operation,void *payload,uint32_t bytes);
} sacx_browser_frontend_api;
static inline const sacx_browser_frontend_api *sacx_browser_frontend(const sacx_api *api){
 const sacx_browser_frontend_api *extension=(const sacx_browser_frontend_api*)api;
 return api && api->struct_size>=sizeof(*extension) && extension->browser_session?extension:0;
}
