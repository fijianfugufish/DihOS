#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Interpreter-only, process-owned sparse VM. Pages are 4 KiB; reservations and
 * alignment are bounded at 4 GiB, with physical backing acquired on commit.
 * Decommit revokes access and returns backing. Touched-span bitmap bookkeeping
 * is lazy. Commit/discard/release work crosses <=64 KiB kernel operations.
 * Multi-batch operations may partially succeed on failure; release remains
 * available. Requires the appended ABI-v1 sparse service (operations 25/26). */
void *dihos_user_vm_reserve(size_t bytes,size_t alignment,unsigned guard_pages);
int dihos_user_vm_commit(void *base,size_t bytes,int writable);
int dihos_user_vm_decommit(void *base,size_t bytes);
int dihos_user_vm_protect(void *base,size_t bytes,int readable,int writable);
int dihos_user_vm_zero(void *base,size_t bytes);
int dihos_user_vm_release(void *base,size_t bytes);
#ifdef __cplusplus
}
#endif
