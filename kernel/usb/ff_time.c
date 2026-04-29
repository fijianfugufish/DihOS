#include <stdint.h>
#include "system/dihos_time.h"

uint32_t get_fattime(void)
{
    return dihos_time_fattime();
}
