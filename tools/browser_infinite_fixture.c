#include <stdint.h>
/* Built with the same basic-block checkpoints as Dihscover. No API calls,
 * explicit yields, function calls or return in this callback. */
void browser_infinite_fixture(void *counter)
{
    volatile uint64_t *value=(volatile uint64_t*)counter;
    for(;;)++*value;
}
