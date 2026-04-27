#pragma once
#include <stdint.h>
#include "usb/ff.h" // ok to expose; your kernel already depends on FatFs

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    FIL fil;
} KFile;
typedef struct
{
    DIR dir;
    FILINFO fi;
} KDir;

enum
{
    KFILE_READ = 1u << 0,
    KFILE_WRITE = 1u << 1,
    KFILE_CREATE = 1u << 2,
    KFILE_TRUNC = 1u << 3,
    KFILE_APPEND = 1u << 4,
};

typedef struct
{
    char name[256];
    uint8_t is_dir;
    uint32_t size;
} kdirent;

/* your API */
int kfile_bind_blockdev(void *blockdev_ptr);
int kfile_mount0(void);
int kfile_umount0(void);

int kfile_open(KFile *f, const char *path, uint32_t flags);
int kfile_read(KFile *f, void *buf, uint32_t n, uint32_t *out_read);
int kfile_write(KFile *f, const void *buf, uint32_t n, uint32_t *out_written);
int kfile_seek(KFile *f, uint64_t offs);
uint64_t kfile_size(KFile *f);
void kfile_close(KFile *f);

int kfile_unlink(const char *p);
int kfile_rename(const char *a, const char *b);
int kfile_mkdir(const char *p);

int kdir_open(KDir *d, const char *path);
int kdir_next(KDir *d, kdirent *out); // 1=entry, 0=end, -1=err
void kdir_close(KDir *d);

#ifdef __cplusplus
}
#endif
