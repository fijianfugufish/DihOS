#pragma once
#include <fontconfig/fontconfig.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Caller owns the returned config. Patterns retain borrowed FT_Face pointers;
 * their owning font buffers/library/faces must outlive the configuration. */
FcConfig* dihos_fc_config_create_memory(void);
FcBool dihos_fc_config_add_pattern(FcConfig*,FcPattern*);
unsigned dihos_fc_io_denials(void);
#ifdef __cplusplus
}
#endif
