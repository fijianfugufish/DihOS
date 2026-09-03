#!/usr/bin/env bash
# Build the first Mesa/Freedreno triangle artifact in the WSL host snapshot.
# This is host-only: DihOS never executes this file or the host compiler it
# invokes.  Tint/texture profiles remain opt-in while their kernel resource
# upload paths are under construction.
set -euo pipefail

project_root="${1:-$PWD}"
project_root="$(cd "$project_root" && pwd)"
mesa_source="${MESART_MESA_SOURCE:-$HOME/mesart-mesa-source}"
mesa_build="${MESART_MESA_BUILD:-$HOME/mesart-mesa-host}"
tool="$mesa_build/mesart_host/mesart_glsl_to_ir3"

if [[ ! -f "$project_root/mesart/host/mesart_glsl_to_ir3.cpp" ]]; then
    echo "Not a DihOS project root: $project_root" >&2
    exit 2
fi
if [[ ! -d "$mesa_source" || ! -d "$mesa_build" ]]; then
    echo "Mesa host snapshot/build directory is missing; run setup_mesart_mesa_host.sh first." >&2
    exit 3
fi

# Copy only Mesart's host tool and ABI envelopes; do not wipe or reconfigure
# the expensive Mesa build directory just to compile another shader pair.
mkdir -p "$mesa_source/mesart_host" "$project_root/build/mesart-shaders"
cp "$project_root/mesart/host/mesart_glsl_to_ir3.cpp" "$mesa_source/mesart_host/"
cp "$project_root/mesart/include/mesart_shader_blob.h" "$mesa_source/mesart_host/"
cp "$project_root/mesart/include/mesart_pipeline_blob.h" "$mesa_source/mesart_host/"
ninja -C "$mesa_build" mesart_host/mesart_glsl_to_ir3

test_names=(triangle)
if [[ "${MESART_INCLUDE_FUTURE_TESTS:-0}" == "1" ]]; then
    test_names+=(tint_triangle textured_triangle)
fi

for test_name in "${test_names[@]}"; do
    "$tool" \
        "$project_root/mesart/host/samples/$test_name.vert" \
        "$project_root/mesart/host/samples/$test_name.frag" \
        "$project_root/build/mesart-shaders/$test_name.vert.mir3" \
        "$project_root/build/mesart-shaders/$test_name.frag.mir3" \
        "$project_root/build/mesart-shaders/$test_name.mpip"
done

echo "Built the purple triangle MIR3/MPIP artifacts under:"
if [[ "${MESART_INCLUDE_FUTURE_TESTS:-0}" != "1" ]]; then
    echo "Tint/texture profiles are intentionally deferred until uniform and texture binding are enabled."
fi
echo "  $project_root/build/mesart-shaders"
