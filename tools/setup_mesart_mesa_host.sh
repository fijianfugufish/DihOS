#!/usr/bin/env bash
# Configure a host-only Mesa/Freedreno build in WSL.  DihOS is not built,
# installed, or modified by this script.  The resulting build directory is
# deliberately under the Linux home directory (not the OneDrive checkout),
# while its source remains the pinned third_party/mesa submodule.
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 /absolute/path/to/arm64-asm-windows-starter" >&2
  exit 2
fi

project_root=$1
mesa_source="$project_root/third_party/mesa"
host_source="$HOME/mesart-mesa-source"
build_root="$HOME/mesart-mesa-host"
mesart_host_dir="$host_source/mesart_host"

if [[ ! -f "$mesa_source/meson.build" ]]; then
  echo "Mesa source not found at: $mesa_source" >&2
  exit 3
fi

# `/mnt/c` preserves Windows CRLF endings.  Mesa runs several Python helpers
# directly by shebang, and Linux correctly rejects `python3\r`.  Build from a
# private Linux-side snapshot instead; the original pinned submodule remains
# untouched.  This bootstrap deliberately never deletes a source tree; a
# future Mesa update can use a new host-source directory after review.
if [[ ! -f "$host_source/meson.build" ]]; then
  mkdir -p "$host_source"
  cp -a "$mesa_source/." "$host_source/"
  find "$host_source" -type f \( -name '*.py' -o -name '*.sh' \) \
    -exec sed -i 's/\r$//' {} +
elif [[ "$mesa_source/VERSION" -nt "$host_source/VERSION" ]]; then
  echo "Host Mesa snapshot is older than the pinned source; stop for review." >&2
  exit 4
fi

# The Mesa snapshot remains its own tree, but receives a tiny tracked Mesart
# host-tool extension.  This is source-only and lives under the Linux home
# directory; it never changes the pinned submodule or contributes files to
# the DihOS image.  Copy on every invocation so the tool and its shared MIR3
# / MPIP format definitions stay synchronized with the checkout.
mkdir -p "$mesart_host_dir"
cp "$project_root/mesart/host/meson.build" "$mesart_host_dir/meson.build"
cp "$project_root/mesart/host/mesart_glsl_to_ir3.cpp" "$mesart_host_dir/mesart_glsl_to_ir3.cpp"
cp "$project_root/mesart/include/mesart_shader_blob.h" "$mesart_host_dir/mesart_shader_blob.h"
cp "$project_root/mesart/include/mesart_pipeline_blob.h" "$mesart_host_dir/mesart_pipeline_blob.h"
if ! grep -Fq "subdir('mesart_host')" "$host_source/meson.build"; then
  printf '\n# Mesart host compiler extension\nsubdir(\x27mesart_host\x27)\n' >> "$host_source/meson.build"
fi

# Keep the host build small: we only need Freedreno's compiler/command-stream
# sources and Mesa's NIR compiler core, not a window system or Mesa's
# standalone developer tools.  Mesa only emits the real NIR implementation
# when an API frontend is enabled; the focused Ninja target below builds the
# compiler archives, not a desktop GL driver.  `computerator` is not part of
# Mesart and remains excluded.
options=(
  -Dplatforms=[]
  -Dopengl=true
  -Dgles1=disabled
  -Dgles2=enabled
  -Dglx=disabled
  -Degl=disabled
  -Dgbm=disabled
  -Dllvm=disabled
  -Dgallium-drivers=freedreno
  -Dvulkan-drivers=[]
  -Dtools=[]
  -Dbuild-tests=false
)

meson setup --wipe "$build_root" "$host_source" "${options[@]}"

echo
echo "Mesa Freedreno host build configured: $build_root"
echo "Next: ninja -C $build_root mesart_host/mesart_glsl_to_ir3"
