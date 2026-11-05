#pragma once
#include "process/dihos_process.h"
typedef struct dihos_user_load_status {
    uint32_t state; /* 0 idle, 1 queued, 2 reading, 3 admitted, 4 failed, 5 cancelled */
    int32_t error;
    uint64_t read_bytes,total_bytes;
    uint64_t read_calls,last_read_ms,max_read_ms;
    dihos_process_handle handle;
} dihos_user_load_status;
int dihos_user_load_start(const char *path);
int dihos_user_load_start_network(const char *path);
int dihos_user_load_start_window(const char *path);
int dihos_user_load_start_web(const char *path);
void dihos_user_load_poll(void);
void dihos_user_load_cancel(void);
void dihos_user_load_status_get(dihos_user_load_status *out);
int dihos_user_load_start_browser(const char *path,uint64_t owner);
int dihos_user_load_browser_result(uint64_t owner,dihos_user_load_status *out);
void dihos_user_load_browser_cancel(uint64_t owner);
