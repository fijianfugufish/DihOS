#pragma once
typedef void (*sighandler_t)(int);
#define SIGPIPE 13
#define SIG_IGN ((sighandler_t)1)
#ifdef __cplusplus
extern "C" {
#endif
sighandler_t signal(int number, sighandler_t handler);
#ifdef __cplusplus
}
#endif
