#pragma once
/* Public builds trust only the pinned Mozilla bundle. A personal snapshot may
 * replace this header using tools/prepare_tls_extra_ca.py. Never edit the
 * public root bundle to distribute a local inspection authority. */
#ifndef DIHOS_TLS_EXTRA_CA_COUNT
#define DIHOS_TLS_EXTRA_CA_COUNT 0u
#define DIHOS_TLS_EXTRA_CA_PEM ""
#define DIHOS_TLS_EXTRA_CA_SHA256 "none"
#endif
