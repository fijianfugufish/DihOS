#include "port/dihscover_profile.h"
static dihscover_phase_stats phases[DIHSCOVER_PHASES];
extern "C" uint64_t dihscover_profile_begin(unsigned phase){(void)phase;return dihscover_active_ms();}
extern "C" void dihscover_profile_end(unsigned phase,uint64_t began){
    if(phase>=DIHSCOVER_PHASES)return;uint64_t ended=dihscover_active_ms(),elapsed=ended>=began?ended-began:0;
    dihscover_phase_stats &s=phases[phase];++s.calls;s.total_ms+=elapsed;if(elapsed>s.max_ms)s.max_ms=elapsed;
}
extern "C" dihscover_phase_stats dihscover_profile_stats(unsigned phase){return phase<DIHSCOVER_PHASES?phases[phase]:dihscover_phase_stats{};}
