#include "ir3/instr-a3xx.h"

#include "mesart_freedreno_ir3.h"

int mesart_fd_ir3_vocabulary_selftest(void)
{
    /* These values and helpers are compiled directly from Mesa's IR3 shader
     * backend.  This is intentionally only a source-compatibility checkpoint:
     * the later NIR-to-IR3 compiler owns instruction scheduling and binary
     * generation, while Mesart's shader BO owns the resulting storage. */
    if (opc_cat(OPC_END) != 0 || opc_op(OPC_END) != 6u ||
        opc_cat(OPC_MOV_IMMED) != 1 || opc_op(OPC_MOV_IMMED) != 40u ||
        opc_cat(OPC_ADD_F) != 2 || opc_op(OPC_ADD_F) != 0u ||
        type_size(TYPE_F16) != 16u || type_size(TYPE_F32) != 32u ||
        type_uint_size(32u) != TYPE_U32 ||
        type_float_size(32u) != TYPE_F32 || !is_cat2_float(OPC_ADD_F) ||
        is_cat2_float(OPC_AND_B))
        return -1;
    return 0;
}
