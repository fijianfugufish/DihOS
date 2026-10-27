#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {DIHSCOVER_PARSE,DIHSCOVER_STYLE,DIHSCOVER_LAYOUT,DIHSCOVER_PAINT,DIHSCOVER_PUBLISH,DIHSCOVER_FETCH,DIHSCOVER_PHASES};
typedef struct dihscover_phase_stats {uint64_t calls,total_ms,max_ms;} dihscover_phase_stats;
uint64_t dihscover_active_ms(void);
uint64_t dihscover_profile_begin(unsigned phase);
void dihscover_profile_end(unsigned phase,uint64_t began);
dihscover_phase_stats dihscover_profile_stats(unsigned phase);
typedef struct dihscover_profile_scope {unsigned phase;uint64_t began;} dihscover_profile_scope;
static inline void dihscover_profile_cleanup(dihscover_profile_scope *scope){dihscover_profile_end(scope->phase,scope->began);}
#define DIHSCOVER_PROFILE_SCOPE(phase_id) dihscover_profile_scope _dihscover_scope __attribute__((cleanup(dihscover_profile_cleanup)))={phase_id,dihscover_profile_begin(phase_id)}
#ifdef __cplusplus
}
#endif
