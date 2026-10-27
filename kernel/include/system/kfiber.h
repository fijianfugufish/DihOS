#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Stackful, same-core continuations. Not interrupt preemption or a sandbox.
 * Yield only outside driver locks and with owned request buffers. */
typedef struct kfiber {
    uint64_t context[40];
    uint64_t caller[40];
    void (*entry)(void *);
    void *argument;
    uint32_t state;
} kfiber;
enum { KFIBER_READY=1, KFIBER_RUNNING, KFIBER_SUSPENDED, KFIBER_DONE };
int kfiber_init(kfiber *fiber,void *stack,uint64_t bytes,void (*entry)(void *),void *argument);
int kfiber_resume(kfiber *fiber);
void kfiber_yield(void);
int kfiber_is_current(const kfiber *fiber);
#ifdef __cplusplus
}
#endif
