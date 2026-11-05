#pragma once
#define O_RDONLY 0
#define O_RDWR 2
#define O_WRONLY 1
#define O_CREAT 0100
#define O_EXCL 0200
#define O_TRUNC 01000
#define O_CLOEXEC 02000000
#define O_BINARY 0
#define F_SETFD 2
#define F_GETFD 1
#define FD_CLOEXEC 1
#define F_WRLCK 1
#define F_SETLKW 7
#define F_SETLK 6
#define F_UNLCK 2
struct flock {short l_type,l_whence;off_t l_start,l_len;pid_t l_pid;};
