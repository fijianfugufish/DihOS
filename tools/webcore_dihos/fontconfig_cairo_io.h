#pragma once
/* Cairo accepts registered FT_Face pointers. Filename-only fallback has no
 * filesystem capability and uses the same counted denial as Fontconfig. */
#ifndef __ASSEMBLER__
int dihos_fc_access(const char*,int);
#define access dihos_fc_access
#endif
