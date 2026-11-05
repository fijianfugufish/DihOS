#include <bmalloc/CryptoRandom.h>
#include "sacx_user_random.h"
#include "sacx_user_api.h"
namespace bmalloc {
void cryptoRandom(void *buffer,size_t length){
    if(sacx_user_random(buffer,length))sacx_user_exit(185,"bmalloc: trusted kernel entropy unavailable");
}
}
