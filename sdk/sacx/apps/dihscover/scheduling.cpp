#include "dihscover.h"
/* Do not instrument this translation unit: it is the compiler checkpoint
 * target itself. Check elapsed time every 1024 basic-block visits. */
static const sacx_api *schedule_api;
static uint32_t countdown=1024;
static uint32_t browser_window,window_checks;
static uint64_t suspended_ms;
extern "C" uint64_t dihscover_active_ms(void){return schedule_api?schedule_api->time_monotonic_ms()-suspended_ms:0;}
extern "C" void dihscover_schedule_api(const sacx_api *api) {schedule_api=api;}
extern "C" void dihscover_schedule_window(uint32_t handle){browser_window=handle;}
extern "C" __attribute__((no_sanitize("coverage"),noinline)) void __sanitizer_cov_trace_pc(void)
{
    if(--countdown)return;
    countdown=1024;
    if(schedule_api&&SACX_API_HAS(schedule_api,app_checkpoint)) {
        /* Closing must not wait for the suspended parser/layout callback to
         * return. The kernel discards its continuation and owns all cleanup. */
        if(browser_window&&!(++window_checks%32)&&schedule_api->window_visible(browser_window)==0)
            schedule_api->app_exit(0,"Dihscover closed");
        uint64_t began=schedule_api->time_monotonic_ms();
        schedule_api->app_checkpoint();
        suspended_ms+=schedule_api->time_monotonic_ms()-began;
    }
}
