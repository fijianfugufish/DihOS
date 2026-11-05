#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sacx_user_api.h>
/* Upstream clip constructors have callers that dereference allocation failure
 * without a status channel. Fail the owning app explicitly instead of letting
 * that path access address zero or silently rendering without its clip. */
void dihos_cairo_clip_allocation_failed(void){
    sacx_user_exit(178,"Cairo: clip allocation exhausted; terminating owning process");
}
/* Cairo duplicates owned metadata strings; the caller releases them normally. */
char* strdup(const char* text){
    size_t size=strlen(text)+1;
    if(!size){errno=ENOMEM;return 0;}
    char* copy=malloc(size);if(copy)memcpy(copy,text,size);return copy;
}
/* ISO C's non-cryptographic PRNG, used only for Cairo cache victim selection.
 * Atomic state avoids a data race when independent surfaces use different
 * cache locks. Security randomness continues to use SACX entropy. */
static uint32_t random_state=1;
void srand(unsigned seed){__atomic_store_n(&random_state,seed,__ATOMIC_RELAXED);}
int rand(void){
    uint32_t old=__atomic_load_n(&random_state,__ATOMIC_RELAXED),next;
    do{next=old*1103515245u+12345u;}
    while(!__atomic_compare_exchange_n(&random_state,&old,next,1,__ATOMIC_RELAXED,__ATOMIC_RELAXED));
    return (int)((next>>16)&0x7fffu);
}
