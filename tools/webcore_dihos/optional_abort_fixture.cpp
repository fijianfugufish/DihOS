#include <optional>
#include "sacx_user_api.h"

// Volatile input prevents constant folding away the actual libc++ check.
static volatile bool engaged=false;
__attribute__((noinline)) static int checked_optional(){
    std::optional<int> value;
    if(engaged)value=1;
    return *value;
}
extern "C" int dihos_user_main(const sacx_api*){return checked_optional();}
