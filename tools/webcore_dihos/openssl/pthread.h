/* Private OpenSSL compatibility boundary. Never installed as DihOS pthread ABI.
 * OpenSSL's supported mutex fallback is used instead of emulated rwlocks. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef uint64_t pthread_t;
typedef uint32_t pthread_key_t;
typedef struct { uint32_t state; } pthread_once_t;
typedef struct { uint32_t word; uint64_t owner; uint32_t depth; int recursive; } pthread_mutex_t;
typedef struct { int recursive; } pthread_mutexattr_t;
typedef struct { uint32_t sequence,waiters; } pthread_cond_t;
#define PTHREAD_ONCE_INIT {0}
#define PTHREAD_MUTEX_INITIALIZER {0,0,0,0}
#define PTHREAD_COND_INITIALIZER {0,0}
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_ERRORCHECK 2
#define pthread_mutexattr_init dihos_ossl_mutexattr_init
#define pthread_mutexattr_destroy dihos_ossl_mutexattr_destroy
#define pthread_mutexattr_settype dihos_ossl_mutexattr_settype
#define pthread_mutex_init dihos_ossl_mutex_init
#define pthread_mutex_destroy dihos_ossl_mutex_destroy
#define pthread_mutex_lock dihos_ossl_mutex_lock
#define pthread_mutex_unlock dihos_ossl_mutex_unlock
#define pthread_cond_init dihos_ossl_cond_init
#define pthread_cond_destroy dihos_ossl_cond_destroy
#define pthread_cond_wait dihos_ossl_cond_wait
#define pthread_cond_signal dihos_ossl_cond_signal
#define pthread_cond_broadcast dihos_ossl_cond_broadcast
#define pthread_once dihos_ossl_once
#define pthread_self dihos_ossl_self
#define pthread_equal dihos_ossl_equal
#define pthread_key_create dihos_ossl_key_create
#define pthread_key_delete dihos_ossl_key_delete
#define pthread_setspecific dihos_ossl_key_set
#define pthread_getspecific dihos_ossl_key_get
int pthread_mutexattr_init(pthread_mutexattr_t*);
int pthread_mutexattr_destroy(pthread_mutexattr_t*);
int pthread_mutexattr_settype(pthread_mutexattr_t*,int);
int pthread_mutex_init(pthread_mutex_t*,const pthread_mutexattr_t*);
int pthread_mutex_destroy(pthread_mutex_t*);
int pthread_mutex_lock(pthread_mutex_t*);
int pthread_mutex_unlock(pthread_mutex_t*);
int pthread_cond_init(pthread_cond_t*,const void*);
int pthread_cond_destroy(pthread_cond_t*);
int pthread_cond_wait(pthread_cond_t*,pthread_mutex_t*);
int pthread_cond_signal(pthread_cond_t*);
int pthread_cond_broadcast(pthread_cond_t*);
int pthread_once(pthread_once_t*,void (*)(void));
pthread_t pthread_self(void);
int pthread_equal(pthread_t,pthread_t);
int pthread_key_create(pthread_key_t*,void (*)(void*));
int pthread_key_delete(pthread_key_t);
int pthread_setspecific(pthread_key_t,const void*);
void* pthread_getspecific(pthread_key_t);
#ifdef __cplusplus
}
#endif
