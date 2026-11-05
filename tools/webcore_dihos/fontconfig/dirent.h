#pragma once
typedef struct dihos_fc_directory DIR;
struct dirent {unsigned char d_type;char d_name[4096];};
#define DT_DIR 4
#define DT_REG 8
#define DT_UNKNOWN 0
