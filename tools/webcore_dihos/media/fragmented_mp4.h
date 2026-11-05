#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Single-owner, incremental ISO BMFF segment assembler. No I/O or kernel
 * pointers. A completed init+moof+mdat view is immutable until consume/reset.
 * Does not advertise MSE support: WebCore track/sample integration is separate. */
typedef struct dihos_fmp4 dihos_fmp4;
enum { DIHOS_FMP4_MORE=0, DIHOS_FMP4_SEGMENT=1 };
enum { DIHOS_FMP4_LIMIT=8*1024*1024, DIHOS_FMP4_INIT_LIMIT=1024*1024 };
int dihos_fmp4_create(dihos_fmp4**);
/* Consumes at most one segment. Caller must retain/retry the unconsumed suffix.
 * SEGMENT with consumed=0 is explicit backpressure, not discarded input. */
int dihos_fmp4_append(dihos_fmp4*, const uint8_t*, size_t, size_t* consumed);
int dihos_fmp4_view(const dihos_fmp4*, const uint8_t** bytes, size_t* size);
/* Available as soon as moov completes, before the first media fragment.
 * Valid until append/reset/close; lets SourceBuffer publish track metadata. */
int dihos_fmp4_initialization(const dihos_fmp4*, const uint8_t** bytes, size_t* size);
/* Increments when a complete initialization is committed. A replacement is
 * staged atomically; abort can restore the last complete initialization. */
uint64_t dihos_fmp4_initialization_version(const dihos_fmp4*);
int dihos_fmp4_consume(dihos_fmp4*);
/* Abort discards incomplete media while retaining a completed init segment.
 * Full reset also releases the init. Both clear a sticky parse error. */
void dihos_fmp4_reset(dihos_fmp4*, int keep_initialization);
/* A clean append boundary with no pending moof/partial header is complete.
 * Ready output must be consumed before finish. Truncated input is an error. */
int dihos_fmp4_finish(dihos_fmp4*);
void dihos_fmp4_close(dihos_fmp4*);
#ifdef __cplusplus
}
#endif
