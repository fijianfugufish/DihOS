#pragma once
#include_next <errno.h>
/* Error numbers shared with the SACX/Linux-compatible negative errno ABI.
 * These constants do not grant POSIX file or network operations. */
#ifndef EAGAIN
#define EAGAIN 11
#endif
#ifndef ENXIO
#define ENXIO 6
#define EXDEV 18
#define ENOEXEC 8
#define ECHILD 10
#define ENOTDIR 20
#define EISDIR 21
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define EFBIG 27
#define EROFS 30
#define EMLINK 31
#define EDEADLK 35
#define ENOLCK 37
#define ENOTEMPTY 39
#endif
#ifndef EBADF
#define EBADF 9
#endif
#ifndef ESPIPE
#define ESPIPE 29
#endif
#ifndef ENAMETOOLONG
#define ENAMETOOLONG 36
#endif
#ifndef ENODATA
#define ENODATA 61
#endif
#ifndef EOVERFLOW
#define EOVERFLOW 75
#endif
#ifndef EPERM
#define EPERM 1
#endif
#ifndef ESRCH
#define ESRCH 3
#endif
#ifndef EINTR
#define EINTR 4
#endif
#ifndef EACCES
#define EACCES 13
#endif
#ifndef EFAULT
#define EFAULT 14
#endif
#ifndef EBUSY
#define EBUSY 16
#endif
#ifndef ENODEV
#define ENODEV 19
#endif
#ifndef ENOSPC
#define ENOSPC 28
#endif
#ifndef EPIPE
#define EPIPE 32
#endif
#ifndef ENOSYS
#define ENOSYS 38
#endif
#ifndef ENOTSUP
#define ENOTSUP 95
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif
