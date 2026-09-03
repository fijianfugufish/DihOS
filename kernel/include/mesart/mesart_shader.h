#pragma once

#include <stdint.h>

#include "mesart_shader_blob.h"

/* Validates only the static artifact envelope.  Package signature and file
 * hash verification happen earlier, when mesart_bundle_open() admits the
 * whole Mesart bundle.  This parser intentionally does not bless a shader
 * for execution; the later A7xx pipeline broker will validate its bindings
 * and produce all CP state itself. */
int mesart_ir3_blob_validate(const void *blob, uint64_t blob_bytes,
                             uint64_t expected_chip_id,
                             mesart_ir3_blob_view *out_view);
