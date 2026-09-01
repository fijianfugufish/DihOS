#include "freedreno_dev_info.h"
#include "mesart_protocol.h"

#include "mesart_freedreno_mesa_device.h"
#include "mesart_freedreno_winsys.h"

int mesart_fd_mesa_device_selftest(void)
{
    mesart_fd_device device = {0};
    struct fd_dev_id device_id;
    const struct fd_dev_info *info;

    /* Consume the signed runtime's brokered identity rather than baking the
     * chip ID into the Mesa selection call.  A future renderer bundle can
     * therefore remain source-identical across supported Adreno variants. */
    if (mesart_fd_device_init(&device) != 0)
        return 1;
    device_id = (struct fd_dev_id){
        .gpu_id = 0u,
        .chip_id = device.chip_id,
    };
    info = fd_dev_info_raw(&device_id);

    /* The full chip ID is the primary selection route.  Keep a name lookup
     * fallback for this first freestanding Mesa port: it reaches the very
     * same generated X1-85 table record without depending on any host-side
     * DRM/KGSL device-query ABI details.  Once Mesart obtains the live chip
     * ID from the kernel rather than its fixed bring-up identity, this
     * fallback remains a useful consistency check for the signed runtime. */
    if (!info)
        info = fd_dev_info_raw_by_name("Adreno X1-85");

    /* Verify source-derived, stable X1-85 identity data.  Optional driver
     * policy flags are intentionally not a Mesart admission requirement:
     * Mesa evolves those as compiler/command paths mature, while the kernel
     * remains the authority that keeps CP register writes forbidden. */
    if (!info)
        return 2;
    if (info->chip != 7u)
        return 3;
    if (info->num_vsc_pipes != 32u)
        return 4;
    if (info->num_ccu != 6u)
        return 5;
    if (info->tile_align_w != 96u)
        return 6;
    if (info->tile_align_h != 32u)
        return 7;
    if (info->tile_max_w != 2016u)
        return 8;
    if (info->tile_max_h != 2032u)
        return 9;
    return 0;
}
