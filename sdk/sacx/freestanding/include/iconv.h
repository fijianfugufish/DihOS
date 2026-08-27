#pragma once
#include <stddef.h>
typedef void *iconv_t;
#ifdef __cplusplus
extern "C" {
#endif
iconv_t iconv_open(const char *to_encoding,const char *from_encoding);
size_t iconv(iconv_t converter,char **input,size_t *input_left,char **output,size_t *output_left);
int iconv_close(iconv_t converter);
#ifdef __cplusplus
}
#endif
