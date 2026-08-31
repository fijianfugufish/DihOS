#include "mesart/mesart_roots.h"
#include "mesart/mesart_development_root.h"
#include "mesart/mesart_release_root.h"

#if defined(DIHOS_BUILD_DEBUG) && DIHOS_BUILD_DEBUG
static const mesart_trust_root g_mesart_root = {
    MESART_DEVELOPMENT_ROOT_BYTES
};
#define MESART_SELECTED_ROOT_CONFIGURED MESART_DEVELOPMENT_ROOT_CONFIGURED
#else
static const mesart_trust_root g_mesart_root = {
    MESART_RELEASE_ROOT_BYTES
};
#define MESART_SELECTED_ROOT_CONFIGURED MESART_RELEASE_ROOT_CONFIGURED
#endif

const mesart_trust_root *mesart_kernel_trust_root(void)
{
#if MESART_SELECTED_ROOT_CONFIGURED
    return g_mesart_root.public_key[0] == 0x04u ? &g_mesart_root : 0;
#else
    return 0;
#endif
}

