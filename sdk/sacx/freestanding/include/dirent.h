#pragma once
typedef struct dihs_dir DIR;
struct dirent { char d_name[256]; };
#ifdef __cplusplus
extern "C" {
#endif
DIR *opendir(const char *path);
struct dirent *readdir(DIR *directory);
int closedir(DIR *directory);
int dirfd(DIR *directory);
#ifdef __cplusplus
}
#endif
