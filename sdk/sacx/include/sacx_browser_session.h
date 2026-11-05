#pragma once
#include <stdint.h>
/* Browser chrome remains in the existing Dihscover frontend. The renderer
   exchanges values and owned pixels only, never callbacks or widget pointers. */
enum { SACX_BROWSER_OPEN=1,SACX_BROWSER_SEND,SACX_BROWSER_STATUS,SACX_BROWSER_CLOSE,
       SACX_BROWSER_RECEIVE,SACX_BROWSER_REPORT,SACX_BROWSER_DESCRIBE,
       SACX_BROWSER_STARTUP_PROGRESS };
enum { SACX_BROWSER_RESIZE=1,SACX_BROWSER_NAVIGATE,SACX_BROWSER_BACK,SACX_BROWSER_FORWARD,
       SACX_BROWSER_RELOAD,SACX_BROWSER_STOP,SACX_BROWSER_PREFS,SACX_BROWSER_MOUSE,
       SACX_BROWSER_KEY,SACX_BROWSER_RESET,SACX_BROWSER_DIAGNOSTICS };
enum { SACX_BROWSER_STARTING=1,SACX_BROWSER_LOADING,SACX_BROWSER_READY,SACX_BROWSER_FAILED,SACX_BROWSER_EXITED };
#define SACX_USER_BROWSER_SESSION 39u
#define SACX_BROWSER_QUEUE 64u
#define SACX_BROWSER_MAX_WIDTH 1920u
#define SACX_BROWSER_MAX_HEIGHT 1080u
typedef struct sacx_browser_command {uint32_t type;int32_t a,b,c,d;char text[768];} sacx_browser_command;
typedef struct sacx_browser_open {uint32_t parent,width,height,session;} sacx_browser_open;
typedef struct sacx_browser_send {uint32_t session,reserved;sacx_browser_command command;} sacx_browser_send;
typedef struct sacx_browser_info {
 uint32_t session,state,width,height,completed,failed,pointer,reserved;
 char title[128],url[768],error[128];
} sacx_browser_info;
/* Optional frontend query. Existing renderer structures and operations retain
   their layout; executable reads are distinct from page/network loading. */
typedef struct sacx_browser_startup_progress {
 uint32_t session,state;
 uint64_t read_bytes,total_bytes;
} sacx_browser_startup_progress;
