#pragma once
#include <stdint.h>
typedef struct dihos_cairo_mutex { uint32_t word,depth; uint64_t owner; } dihos_cairo_mutex;
#ifdef __cplusplus
extern "C" {
#endif
void dihos_cairo_mutex_init(dihos_cairo_mutex*);
void dihos_cairo_mutex_fini(dihos_cairo_mutex*);
void dihos_cairo_mutex_lock(dihos_cairo_mutex*);
int dihos_cairo_mutex_trylock(dihos_cairo_mutex*);
void dihos_cairo_mutex_unlock(dihos_cairo_mutex*);
#ifdef __cplusplus
}
#endif
typedef dihos_cairo_mutex cairo_mutex_impl_t;
typedef dihos_cairo_mutex cairo_recursive_mutex_impl_t;
#define CAIRO_MUTEX_IMPL_DIHOS 1
#define CAIRO_MUTEX_HAS_RECURSIVE_IMPL 1
#define CAIRO_MUTEX_IMPL_NIL_INITIALIZER { 0,0,0 }
#define CAIRO_RECURSIVE_MUTEX_IMPL_NIL_INITIALIZER { 0,0,0 }
#define CAIRO_MUTEX_IMPL_INIT(mutex) dihos_cairo_mutex_init(&(mutex))
#define CAIRO_RECURSIVE_MUTEX_IMPL_INIT(mutex) dihos_cairo_mutex_init(&(mutex))
#define CAIRO_MUTEX_IMPL_FINI(mutex) dihos_cairo_mutex_fini(&(mutex))
#define CAIRO_MUTEX_IMPL_LOCK(mutex) dihos_cairo_mutex_lock(&(mutex))
#define CAIRO_MUTEX_IMPL_TRY_LOCK(mutex) dihos_cairo_mutex_trylock(&(mutex))
#define CAIRO_MUTEX_IMPL_UNLOCK(mutex) dihos_cairo_mutex_unlock(&(mutex))
