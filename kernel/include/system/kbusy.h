#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    void kbusy_begin(void);
    void kbusy_pump(void);
    void kbusy_end(void);
    int kbusy_active(void);

#ifdef __cplusplus
}
#endif
