#pragma once
#include "sacx_user_syscall.h"
/* ABI v1 append-only sparse service. No executable pages. Reserve is virtual;
 * commit/decommit/protect use the existing <=64 KiB memory operations. Release
 * revokes at most 64 KiB per call and returns -11 while more work remains.
 * The pointer remains reserved until release returns zero. */
#if defined(__aarch64__)
static inline int64_t sacx_user_sparse_reserve(uint64_t bytes,uint64_t alignment,unsigned guards){
    return sacx_user_call(SACX_USER_SPARSE_RESERVE,bytes,alignment,guards);
}
static inline int sacx_user_sparse_release_step(uint64_t address){
    return (int)sacx_user_call(SACX_USER_SPARSE_RELEASE,address,0,0);
}
static inline int sacx_user_sparse_release(uint64_t address){
    int result;do {result=sacx_user_sparse_release_step(address);}while(result==-11);return result;
}
#endif
