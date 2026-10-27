#include <cassert>
#include <cstdio>
#include "sacx_api.h"
#include "../sdk/sacx/apps/dihscover/port/dihscover_profile.h"
extern "C" void dihscover_schedule_api(const sacx_api*);
extern "C" void dihscover_schedule_window(uint32_t);
extern "C" void __sanitizer_cov_trace_pc(void);
static uint64_t now=100;static unsigned slices,closed;
static uint64_t elapsed(){return now;}
static int checkpoint(){++slices;now+=20;return 0;}
static int visible(uint32_t){return 0;}
static int finish(int,const char*){++closed;return 0;}
static void scoped_return(){DIHSCOVER_PROFILE_SCOPE(DIHSCOVER_PARSE);now+=3;return;}
int main(){
 sacx_api api={};api.struct_size=sizeof(api);api.time_monotonic_ms=elapsed;api.app_checkpoint=checkpoint;api.window_visible=visible;api.app_exit=finish;
 dihscover_schedule_api(&api);dihscover_schedule_window(1);
 for(unsigned i=0;i<32768;++i)__sanitizer_cov_trace_pc();
 assert(slices==32&&closed==1&&now==740&&dihscover_active_ms()==100);
 auto start=dihscover_profile_begin(DIHSCOVER_STYLE);now+=7;dihscover_profile_end(DIHSCOVER_STYLE,start);
 auto stats=dihscover_profile_stats(DIHSCOVER_STYLE);assert(stats.calls==1&&stats.total_ms==7&&stats.max_ms==7);
 scoped_return();stats=dihscover_profile_stats(DIHSCOVER_PARSE);assert(stats.calls==1&&stats.total_ms==3);
 dihscover_profile_end(999,start);assert(!dihscover_profile_stats(999).calls);
 puts("PASS: production checkpoint cadence, closing during a suspended callback, elapsed profiling excludes scheduler pauses and scoped early-return accounting");
}
