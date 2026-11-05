#include "cairo_mutex_dihos.h"
#include <sacx_user_api.h>
#include <sacx_user_threads.h>
static __thread uint64_t cached_self;
static void fail(const char* text){sacx_user_exit(176,text);}
static uint64_t self(void){
    if(!cached_self){cached_self=sacx_user_thread_self();if(!cached_self || (int64_t)cached_self<0)fail("Cairo: thread identity unavailable");}
    return cached_self;
}
void dihos_cairo_mutex_init(dihos_cairo_mutex* m){m->word=0;m->depth=0;m->owner=0;}
void dihos_cairo_mutex_fini(dihos_cairo_mutex* m){
    if(__atomic_load_n(&m->word,__ATOMIC_RELAXED) || __atomic_load_n(&m->owner,__ATOMIC_RELAXED) || m->depth)fail("Cairo: destroying held mutex");
}
static int recurse(dihos_cairo_mutex* m,uint64_t id){
    if(__atomic_load_n(&m->owner,__ATOMIC_RELAXED)!=id)return 0;
    if(m->depth==UINT32_MAX)fail("Cairo: recursive lock overflow");
    ++m->depth;return 1;
}
void dihos_cairo_mutex_lock(dihos_cairo_mutex* m){
    uint64_t id=self();if(recurse(m,id))return;
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&m->word,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
        while(__atomic_exchange_n(&m->word,2,__ATOMIC_ACQUIRE)){
            int rc=sacx_user_wait32(&m->word,2,60000);
            if(rc && rc!=-11 && rc!=-110)fail("Cairo: mutex wait failed");
        }
    }
    m->depth=1;__atomic_store_n(&m->owner,id,__ATOMIC_RELAXED);
}
int dihos_cairo_mutex_trylock(dihos_cairo_mutex* m){
    uint64_t id=self();if(recurse(m,id))return 1;
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&m->word,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;
    m->depth=1;__atomic_store_n(&m->owner,id,__ATOMIC_RELAXED);return 1;
}
void dihos_cairo_mutex_unlock(dihos_cairo_mutex* m){
    if(__atomic_load_n(&m->owner,__ATOMIC_RELAXED)!=self() || !m->depth)fail("Cairo: foreign mutex unlock");
    if(--m->depth)return;
    __atomic_store_n(&m->owner,0,__ATOMIC_RELAXED);
    if(__atomic_exchange_n(&m->word,0,__ATOMIC_RELEASE)==2 && sacx_user_wake32(&m->word,4)<0)fail("Cairo: mutex wake failed");
}
