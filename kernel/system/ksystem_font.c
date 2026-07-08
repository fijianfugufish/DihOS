#include "system/ksystem_font.h"
#include "system/ksystem_font_data.h"

int ksystem_font_init_fallback(kfont *out)
{
    return ktext_load_psf_blob(g_ksystem_fallback_psf, g_ksystem_fallback_psf_size, out);
}

int ksystem_font_load_system_file(const char *path, kfont *out, void **out_blob, uint32_t *out_size)
{
    return ktext_load_psf_file(path, out, out_blob, out_size);
}
