#include "usb/ff.h"

#if FF_USE_LFN >= 1

/*
 * Lightweight OEM <-> Unicode bridge for this kernel build.
 *
 * This keeps ASCII semantics exact and preserves extended single-byte and
 * packed DBCS values as-is so long-name APIs can work without the full
 * FatFs conversion tables.
 */
WCHAR ff_uni2oem(DWORD uni, WORD cp)
{
    (void)cp;
    if (uni == 0u || uni > 0xFFFFu)
        return 0u;
    return (WCHAR)uni;
}

WCHAR ff_oem2uni(WCHAR oem, WORD cp)
{
    (void)cp;
    return oem;
}

DWORD ff_wtoupper(DWORD uni)
{
    if (uni >= 'a' && uni <= 'z')
        return uni - 0x20u;

    /* Latin-1 lowercase letters except division sign. */
    if (uni >= 0x00E0u && uni <= 0x00FEu && uni != 0x00F7u)
        return uni - 0x20u;

    return uni;
}

#endif
