#pragma once
#include <stdint.h>
#include <stddef.h>
#include "bootinfo.h"
#define DIHOS_ENTROPY_MAX 256u
#define DIHOS_ENTROPY_RESEED_CALLS (1ull<<48)
#define DIHOS_ENTROPY_REFRESH_CALLS 65536u
enum {DIHOS_ENTROPY_FIRMWARE=1,DIHOS_ENTROPY_RAW=2,DIHOS_ENTROPY_RNDR=4};
typedef struct {uint32_t ready,sources,hardware,last_error;uint64_t requests,reseeds,firmware_status;} dihos_entropy_info;
void dihos_entropy_init(boot_info *boot);
int dihos_entropy_fill(void *output,size_t bytes);
void dihos_entropy_status(dihos_entropy_info *info);
#ifdef DIHOS_ENTROPY_TEST
void dihos_entropy_test_reset(void);
void dihos_entropy_test_requests(uint64_t requests);
int dihos_entropy_test_word(uint64_t *word);
int dihos_entropy_test_hardware(void);
#endif
